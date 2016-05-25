/*
 * Copyright 2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * tx_callbacks.c -- transaction callback subsystem
 */

#include "libpmemobj.h"
#include "out.h"
#include "util.h"
#include "redo.h"
#include "lane.h"
#include "memops.h"
#include "pmalloc.h"
#include "list.h"
#include "obj.h"

#include <pthread.h>
#include <errno.h>

struct tx_callback {
	pmemobj_tx_callback_basic func;
	void *arg;
};

struct tx_callback_array {
	struct tx_callback *arr;

	/* Size of callbacks array. */
	unsigned size;

	/* Number of registered callbacks. */
	unsigned used;
};

struct all_callbacks {
	struct tx_callback_array forward;
	struct tx_callback_array backward;
};

static pthread_key_t callbacks_key;

/*
 * obj_tx_get_callbacks -- returns current per-thread callback configuration
 */
static struct all_callbacks *
obj_tx_get_callbacks(void)
{
	struct all_callbacks *c = pthread_getspecific(callbacks_key);
	if (!c) {
		c = Zalloc(sizeof(struct all_callbacks) * MAX_TX_STAGE);
		int ret = pthread_setspecific(callbacks_key, c);
		if (ret) {
			errno = ret;
			FATAL("!pthread_setspecific");
		}
	}

	return c;
}

__thread int obj_basic_tx_callback;

/*
 * obj_tx_callback_check -- (internal) check whether current state
 * allows changing transaction callbacks
 */
static void
obj_tx_callback_check(const char *func)
{
	if (pmemobj_tx_stage() == TX_STAGE_NONE)
		FATAL("%s called outside of transaction", func);

	if (!obj_basic_tx_callback)
		FATAL("Transaction does not use default callback");
}

/*
 * obj_tx_callback_append -- (internal) appends callback to specified array
 */
static int
obj_tx_callback_append(struct tx_callback_array *cb,
		pmemobj_tx_callback_basic func,
		void *arg)
{
	if (cb->used == cb->size) {
		unsigned count = cb->size * 2;
		if (count == 0)
			count = 4;

		void *new_arr = Realloc(cb->arr, count * sizeof(cb->arr[0]));
		if (!new_arr) {
			pmemobj_tx_abort(errno);
			return -1;
		}

		cb->arr = new_arr;
		cb->size = count;
	}

	unsigned num = cb->used++;
	cb->arr[num].func = func;
	cb->arr[num].arg = arg;

	return 0;
}

/*
 * pmemobj_tx_stage_callback_push_back -- registers transaction stage callback,
 * to be called at the end
 */
int
pmemobj_tx_stage_callback_push_back(enum pobj_tx_stage stage,
		pmemobj_tx_callback_basic func,
		void *arg)
{
	LOG(15, NULL);

	obj_tx_callback_check(__func__);
	struct all_callbacks *cbs = obj_tx_get_callbacks();

	return obj_tx_callback_append(&cbs[stage].forward, func, arg);
}

/*
 * pmemobj_tx_stage_callback_push_front -- registers transaction stage callback,
 * to be called at the beginning
 */
int
pmemobj_tx_stage_callback_push_front(enum pobj_tx_stage stage,
		pmemobj_tx_callback_basic func,
		void *arg)
{
	LOG(15, NULL);

	obj_tx_callback_check(__func__);
	struct all_callbacks *cbs = obj_tx_get_callbacks();

	return obj_tx_callback_append(&cbs[stage].backward, func, arg);
}

/*
 * file_tx_callbacks_free -- frees tx callbacks
 */
static void
obj_tx_callbacks_free(void *arg)
{
	LOG(15, NULL);
	struct all_callbacks *callbacks = arg;

	for (unsigned i = 0; i < MAX_TX_STAGE; ++i) {
		Free(callbacks[i].forward.arr);
		callbacks[i].forward.arr = NULL;
		callbacks[i].forward.size = 0;
		callbacks[i].forward.used = 0;

		Free(callbacks[i].backward.arr);
		callbacks[i].backward.arr = NULL;
		callbacks[i].backward.size = 0;
		callbacks[i].backward.used = 0;
	}

	Free(callbacks);

	int ret = pthread_setspecific(callbacks_key, NULL);
	if (ret) {
		errno = ret;
		FATAL("!pthread_setspecific");
	}
}

/*
 * obj_tx_callbacks_init -- initializes callbacks subsystem
 */
void
obj_tx_callbacks_init(void)
{
	int ret = pthread_key_create(&callbacks_key, obj_tx_callbacks_free);
	if (ret)
		FATAL("!pthread_key_create");
}

/*
 * obj_tx_callbacks_fini -- cleans up state of tx callback module
 */
void
obj_tx_callbacks_fini(void)
{
	struct all_callbacks *c = pthread_getspecific(callbacks_key);
	if (c)
		obj_tx_callbacks_free(c);
}

/*
 * pmemobj_tx_stage_callback_basic -- basic implementation of transaction
 * callback that calls multiple functions
 */
void
pmemobj_tx_stage_callback_basic(enum pobj_tx_stage stage, void *arg)
{
	LOG(15, NULL);

	struct tx_callback_array *cb;
	unsigned num_callbacks;
	struct all_callbacks *file_callbacks = obj_tx_get_callbacks();

	cb = &file_callbacks[stage].backward;
	num_callbacks = cb->used;
	if (num_callbacks) {
		struct tx_callback *c = cb->arr;
		for (unsigned i = num_callbacks; i > 0; --i)
			c[i - 1].func(arg, c[i - 1].arg);
	}

	cb = &file_callbacks[stage].forward;
	num_callbacks = cb->used;
	if (num_callbacks) {
		struct tx_callback *c = cb->arr;
		for (unsigned i = 0; i < num_callbacks; ++i)
			c[i].func(arg, c[i].arg);
	}

	if (stage == TX_STAGE_NONE) {
		for (unsigned i = 0; i < MAX_TX_STAGE; ++i) {
			file_callbacks[i].backward.used = 0;
			file_callbacks[i].forward.used = 0;
		}

		obj_basic_tx_callback = 0;
	}
}
