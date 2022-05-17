#ifndef C_FEK_BLOCKING_QUEUE
#define C_FEK_BLOCKING_QUEUE

/*
	Author: Felipe Einsfeld Kersting

	MIT License

	Copyright (c) 2020 Felipe Kersting

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.

	To use this blocking queue, define C_FEK_BLOCKING_QUEUE_IMPLEMENTATION before including blocking_queue.h in one of your source files.

	To use this blocking queue, you must link your binary with pthread.

	Note that fair_lock.h is a pre-requisite for this implementation, so you also need to include fair_lock.h in one of your source files
	and define C_FEK_FAIR_LOCK_IMPLEMENTATION before including it.

	This blocking queue is thread-safe.

	This blocking queue has the following properties/features:

	- It has a fixed capacity, provided by the caller.
	- It allows the caller to add elements to the queue via both a blocking call and a non-blocking call.
	- It allows the caller to get elements from the queue via both a blocking call and a non-blocking call.
	- If multiple callers are blocked adding/getting an element to/from the queue, they are served in FIFO order.
	
	The last point avoids the problem of starvation.

	Note that since the queue is designed to serve callers in FIFO order, this might have a significant impact in performance.

	If FIFO is not needed in your implementation, consider using a queue with no such guarantee.

	Define C_FEK_BLOCKING_QUEUE_NO_CRT if you don't want the C Runtime Library included. If this is defined, you must provide
	implementations for the following functions:

	void* malloc(unsigned int size)
	void  free(void* block)
	void* memcpy (void* dest, const void* src, unsigned int n)

	For more information about the API, check the comments in the function signatures.

	An usage example:

	#define C_FEK_BLOCKING_QUEUE_IMPLEMENTATION
	#define C_FEK_FAIR_LOCK_IMPLEMENTATION
	#include "blocking_queue.h"
	#include <assert.h>

	int main() {
		Blocking_Queue bq;
		int aux;

		blocking_queue_init(&bq, 4);
		
		// Example of non-blocking calls
		blocking_queue_add(&bq, (void*)1);
		blocking_queue_poll(&bq, &aux);
		assert(aux == 1);

		// Example of potentially blocking calls
		blocking_queue_put(&bq, (void*)2);
		blocking_queue_take(&bq, &aux);
		assert(aux == 2);

		blocking_queue_destroy(&bq);
		return 0;
	}

	Another example - more similar to a real use-case - since this time the integer is allocated:

	#define C_FEK_BLOCKING_QUEUE_IMPLEMENTATION
	#define C_FEK_FAIR_LOCK_IMPLEMENTATION
	#include "blocking_queue.h"
	#include <assert.h>

	int main() {
		Blocking_Queue bq;
		int* aux = malloc(sizeof(int));
		int* aux2;

		blocking_queue_init(&bq, 4);
		
		// Example of non-blocking calls
		*aux = 1;
		blocking_queue_add(&bq, aux);
		blocking_queue_poll(&bq, &aux2);
		assert(*aux2 == 1);

		// Example of potentially blocking calls
		*aux = 2;
		blocking_queue_put(&bq, aux);
		blocking_queue_take(&bq, &aux2);
		assert(*aux2 == 2);

		blocking_queue_destroy(&bq);
		free(aux);
		return 0;
	}

	For complete examples, check the tests in the repository https://github.com/felipeek/c-fifo-blocking-queue
*/

#include "fair_lock.h"

#define BQ_ERROR 1
#define BQ_FULL 2
#define BQ_EMPTY 3
#define BQ_CLOSED 4

// This structure is reserved for internal-use only
typedef struct {
	// Fair lock used for get operations
	Fair_Lock get_lock;
	// Fair lock used for add operations
	Fair_Lock add_lock;
	// Stores whether weak locks are blocked for the 'get_lock'. Used as an optimization
	int get_lock_are_weak_locks_blocked;
	// Stores whether weak locks are blocked for the 'add_lock'. Used as an optimization
	int add_lock_are_weak_locks_blocked;
	// Main mutex, synchronizes get/add operations.
	pthread_mutex_t mutex;
	// Cond used to wake up blocked callers
	pthread_cond_t cond;
	// The queue of elements. It has a fixed size and is allocated in the init call.
	// This is a circular queue. The front and the rear of the queue are given by queue_front and queue_rear
	void** queue;
	// The capacity of the queue
	unsigned int queue_capacity;
	// Number of elements currently in the queue.
	unsigned int queue_size;
	// The front of the queue
	unsigned int queue_front;
	// The rear of the queue
	unsigned int queue_rear;
	// If true, the queue does not have a maximum capacity
	int is_boundless;
	// Number of active callers. Used mainly to synchronize the destroy process.
	int active_callers_count;
	// Indicates whether the queue was closed.
	int closed;
	// Mutex to change 'active_callers_count'
	pthread_mutex_t active_callers_mutex;
	// Cond to help synchronizing the destroy process
	pthread_cond_t destroy_cond;
	// Auxiliar mutex to make the 'close' call thread-safe
	pthread_mutex_t close_mutex;
} Blocking_Queue;

// Init the blocking queue.
// The blocking queue capacity is given by 'capacity'.
// When capacity > 0, a normal FIFO blocking queue with fixed size is used.
// When capacity <= 0, the queue will not have a maximum cap. It will, instead, grow without bounds (this is a special case)
// Note that, in the special case, the queue will serve as a normal boundless thread-safe queue (no blocking will ever occur when adding elements)
// Returns 0 if success, -1 if error.
int blocking_queue_init(Blocking_Queue* bq, unsigned int capacity);
// Adds an element to the blocking queue
// The element is given by 'element'
// This function does NOT block the caller.
// If the queue is full, this function will not add the new element. Instead, it will return BQ_FULL.
// FIFO order is guaranteed - blocked callers will be served in FIFO order. There is no starvation.
// Returns:
// * 0 if success
// * BQ_ERROR if an error happened
// * BQ_FULL if the there is no space in the blocking queue
// * BQ_CLOSED if the blocking queue was closed while the call was blocked
int blocking_queue_add(Blocking_Queue* bq, void* element);
// Puts an element to the blocking queue
// The element is given by 'element'
// This function may block the caller.
// If the queue is full, the caller is blocked until there is space in the queue for the new element.
// FIFO order is guaranteed - blocked callers will be served in FIFO order. There is no starvation.
// Returns:
// * 0 if success
// * BQ_ERROR if an error happened
// * BQ_CLOSED if the blocking queue was closed while the call was blocked
int blocking_queue_put(Blocking_Queue* bq, void* element);
// Poll an element from the blocking queue
// The element is stored in '*element'
// This function does NOT block the caller.
// If the queue is empty, this function will not poll any element. Instead, it will return BQ_EMPTY.
// FIFO order is guaranteed - blocked callers will be served in FIFO order. There is no starvation.
// Returns:
// * 0 if success
// * BQ_ERROR if an error happened
// * BQ_EMPTY if the blocking queue is empty
// * BQ_CLOSED if the blocking queue was closed while the call was blocked
int blocking_queue_poll(Blocking_Queue* bq, void* element);
// Take an element from the blocking queue
// The element is stored in '*element'
// This function may block the caller.
// If the queue is empty, the caller is blocked until there is an element available to take.
// FIFO order is guaranteed - blocked callers will be served in FIFO order. There is no starvation.
// Returns:
// * 0 if success
// * BQ_ERROR if an error happened
// * BQ_CLOSED if the blocking queue was closed while the call was blocked
int blocking_queue_take(Blocking_Queue* bq, void* element);
// Closes the blocking queue.
// When a blocking queue is closed, all _add/_put/_poll/_take calls will immediately return BQ_CLOSED if called.
// If there are active callers blocked in one of these calls, they will also be immediately unblocked and receive BQ_CLOSED.
// After the blocking queue is closed, it cannot be reopened again. The only thing remaining to do is calling 'blocking_queue_destroy'
// to free up the resources.
// This call is an auxiliar call that was introduced just to help solving synchronization issues. It helps the caller to close the queue, while
// keeping its reference valid. If 'blocking_queue_destroy' is called directly, it would first close the queue just like this function does,
// but after all callers return it would immediately free up the resources. This function allows the caller to split the closing of the queue
// and the freeing up of the resources into two calls, so the blocking queue reference is still valid after the queue is closed.
void blocking_queue_close(Blocking_Queue* bq);
// Destroys the blocking queue.
// If the queue is not closed (see 'blocking_queue_close'), it will first close the queue. Closing the queue will make all
// _add/_put_/_poll/_take calls to return immediately with BQ_CLOSED status. For more information, check 'blocking_queue_close'.
// After closing the queue and waiting for all active callers to return, the resources are freed.
// After this function is called, the blocking queue **cannot** be used anymore.
// Using it will cause undefined behavior and may crash the program.
// NOTE: This function can only be called a single time for a given blocking queue. Calling it multiple times will cause undefined behavior.
void blocking_queue_destroy(Blocking_Queue* bq);

#ifdef C_FEK_BLOCKING_QUEUE_IMPLEMENTATION
#if !defined(C_FEK_BLOCKING_QUEUE_NO_CRT)
#include <stdlib.h>
#include <memory.h>
#endif

int blocking_queue_init(Blocking_Queue* bq, unsigned int capacity)
{
	if (pthread_mutex_init(&bq->mutex, NULL)) {
		return -1;
	}

	if (pthread_mutex_init(&bq->active_callers_mutex, NULL)) {
		pthread_mutex_destroy(&bq->mutex);
		return -1;
	}

	if (pthread_mutex_init(&bq->close_mutex, NULL)) {
		pthread_mutex_destroy(&bq->mutex);
		pthread_mutex_destroy(&bq->active_callers_mutex);
		return -1;
	}

	if (pthread_cond_init(&bq->cond, NULL)) {
		pthread_mutex_destroy(&bq->mutex);
		pthread_mutex_destroy(&bq->active_callers_mutex);
		pthread_mutex_destroy(&bq->close_mutex);
		return -1;
	}

	if (pthread_cond_init(&bq->destroy_cond, NULL)) {
		pthread_mutex_destroy(&bq->mutex);
		pthread_mutex_destroy(&bq->active_callers_mutex);
		pthread_mutex_destroy(&bq->close_mutex);
		pthread_cond_destroy(&bq->cond);
		return -1;
	}

	if (fair_lock_init(&bq->get_lock)) {
		pthread_mutex_destroy(&bq->mutex);
		pthread_mutex_destroy(&bq->active_callers_mutex);
		pthread_mutex_destroy(&bq->close_mutex);
		pthread_cond_destroy(&bq->cond);
		pthread_cond_destroy(&bq->destroy_cond);
		return -1;
	}

	if (fair_lock_init(&bq->add_lock)) {
		pthread_mutex_destroy(&bq->mutex);
		pthread_mutex_destroy(&bq->active_callers_mutex);
		pthread_mutex_destroy(&bq->close_mutex);
		pthread_cond_destroy(&bq->cond);
		pthread_cond_destroy(&bq->destroy_cond);
		fair_lock_destroy(&bq->get_lock);
		return -1;
	}

	if (capacity <= 0) {
		bq->queue_capacity = 1;
		bq->is_boundless = 1;
	} else {
		bq->queue_capacity = capacity;
		bq->is_boundless = 0;
	}
	bq->queue_size = 0;
	bq->queue_front = 0;
	bq->queue_rear = bq->queue_capacity - 1;
	bq->closed = 0;
	bq->active_callers_count = 0;
	bq->get_lock_are_weak_locks_blocked = 0;
	bq->add_lock_are_weak_locks_blocked = 0;
	bq->queue = (void**)malloc(bq->queue_capacity * sizeof(void*));
	if (bq->queue == NULL)
	{
		pthread_mutex_destroy(&bq->mutex);
		pthread_mutex_destroy(&bq->active_callers_mutex);
		pthread_mutex_destroy(&bq->close_mutex);
		pthread_cond_destroy(&bq->cond);
		pthread_cond_destroy(&bq->destroy_cond);
		fair_lock_destroy(&bq->get_lock);
		fair_lock_destroy(&bq->add_lock);
		return -1;
	}
	
	return 0;
}

void blocking_queue_close(Blocking_Queue* bq) {
	pthread_mutex_lock(&bq->close_mutex);
	pthread_mutex_lock(&bq->mutex);
	if (bq->closed) {
		pthread_mutex_unlock(&bq->mutex);
		pthread_mutex_unlock(&bq->close_mutex);
		return;
	}
	bq->closed = 1;
	pthread_mutex_unlock(&bq->mutex);

	pthread_mutex_lock(&bq->active_callers_mutex);
	while (bq->active_callers_count) {
		pthread_cond_signal(&bq->cond);
		pthread_cond_wait(&bq->destroy_cond, &bq->active_callers_mutex);
	}
	pthread_mutex_unlock(&bq->active_callers_mutex);
	pthread_mutex_unlock(&bq->close_mutex);
}

void blocking_queue_destroy(Blocking_Queue* bq) {
	blocking_queue_close(bq);
	free(bq->queue);
	fair_lock_destroy(&bq->get_lock);
	fair_lock_destroy(&bq->add_lock);
	pthread_mutex_destroy(&bq->mutex);
	pthread_mutex_destroy(&bq->close_mutex);
}

static void enqueue(Blocking_Queue *bq, void *element) {
	//assert(bq->queue_size < bq->queue_capacity);
	bq->queue_rear = (bq->queue_rear + 1) % bq->queue_capacity;
	bq->queue[bq->queue_rear] = element;
	bq->queue_size = bq->queue_size + 1;
}

static void *dequeue(Blocking_Queue *bq) {
  //assert(bq->queue_size > 0);

  void* element = bq->queue[bq->queue_front];
  bq->queue_front = (bq->queue_front + 1) % bq->queue_capacity;
  bq->queue_size = bq->queue_size - 1;
  return element;
}

static void increase_active_callers_count(Blocking_Queue* bq) {
	pthread_mutex_lock(&bq->active_callers_mutex);
	++bq->active_callers_count;
	pthread_mutex_unlock(&bq->active_callers_mutex);
}

static void decrease_active_callers_count(Blocking_Queue* bq) {
	pthread_mutex_lock(&bq->active_callers_mutex);
	--bq->active_callers_count;
	pthread_cond_signal(&bq->destroy_cond);
	pthread_mutex_unlock(&bq->active_callers_mutex);
}

static int grow_queue(Blocking_Queue* bq) {
	unsigned int new_capacity = bq->queue_capacity * 2u;
	void** new_queue = (void**)malloc(new_capacity * sizeof(void*));
	if (new_queue == NULL) {
		return 1;
	}

	if (bq->queue_rear >= bq->queue_front) {
		memcpy(new_queue, bq->queue + bq->queue_front, bq->queue_size * sizeof(void*));
	} else if (bq->queue_front > bq->queue_rear) {
		memcpy(new_queue, bq->queue + bq->queue_front, (bq->queue_capacity - bq->queue_front) * sizeof(void*));
		memcpy(new_queue + (bq->queue_capacity - bq->queue_front), bq->queue, (bq->queue_rear + 1) * sizeof(void*));
	}

	free(bq->queue);
	bq->queue = new_queue;
	bq->queue_capacity = new_capacity;
	bq->queue_front = 0;
	bq->queue_rear = bq->queue_size - 1;
	return 0;
}

int blocking_queue_add_internal(Blocking_Queue* bq, void* element, int async) {
	increase_active_callers_count(bq);

	int lock_ret;
	if (async) {
		lock_ret = fair_lock_lock_weak(&bq->add_lock);
	} else {
		lock_ret = fair_lock_lock(&bq->add_lock);
	}

	//assert(lock_ret == 0 || lock_ret == FL_ERROR || lock_ret == FL_ABANDONED);

	if (lock_ret == FL_ERROR) {
		decrease_active_callers_count(bq);
		return BQ_ERROR;
	} else if (lock_ret == FL_ABANDONED) {
		decrease_active_callers_count(bq);
		return BQ_FULL;
	}

	pthread_mutex_lock(&bq->mutex);

	if (bq->closed) {
		fair_lock_unlock(&bq->add_lock);
		pthread_mutex_unlock(&bq->mutex);
		decrease_active_callers_count(bq);
		return BQ_CLOSED;
	}

	if (bq->queue_size == bq->queue_capacity) {
		if (bq->is_boundless) {
			if (grow_queue(bq)) {
				fair_lock_unlock(&bq->add_lock);
				pthread_mutex_unlock(&bq->mutex);
				decrease_active_callers_count(bq);
				return BQ_ERROR;
			}
		} else {
			if (!bq->add_lock_are_weak_locks_blocked) {
				fair_lock_block_weak_locks(&bq->add_lock);
				bq->add_lock_are_weak_locks_blocked = 1;
			}
			if (async) {
				fair_lock_unlock(&bq->add_lock);
				pthread_mutex_unlock(&bq->mutex);
				decrease_active_callers_count(bq);
				return BQ_FULL;
			}
			pthread_cond_wait(&bq->cond, &bq->mutex);
			if (bq->closed) {
				fair_lock_unlock(&bq->add_lock);
				pthread_mutex_unlock(&bq->mutex);
				decrease_active_callers_count(bq);
				return BQ_CLOSED;
			}
			//assert(bq->queue_size < bq->queue_capacity);
		}
	}
	if (bq->get_lock_are_weak_locks_blocked) {
		fair_lock_allow_weak_locks(&bq->get_lock);
		bq->get_lock_are_weak_locks_blocked = 0;
	}
	pthread_cond_signal(&bq->cond);
	enqueue(bq, element);
	pthread_mutex_unlock(&bq->mutex);

	fair_lock_unlock(&bq->add_lock);

	decrease_active_callers_count(bq);

	return 0;
}

int blocking_queue_get_internal(Blocking_Queue* bq, int async, void* element) {
	increase_active_callers_count(bq);

	int lock_ret;
	if (async) {
		lock_ret = fair_lock_lock_weak(&bq->get_lock);
	} else {
		lock_ret = fair_lock_lock(&bq->get_lock);
	}

	if (lock_ret == FL_ERROR) {
		decrease_active_callers_count(bq);
		return BQ_ERROR;
	} else if (lock_ret == FL_ABANDONED) {
		decrease_active_callers_count(bq);
		return BQ_EMPTY;
	}

	//assert(lock_ret == 0 || lock_ret == FL_ERROR || lock_ret == FL_ABANDONED);

	pthread_mutex_lock(&bq->mutex);

	if (bq->closed) {
		fair_lock_unlock(&bq->get_lock);
		pthread_mutex_unlock(&bq->mutex);
		decrease_active_callers_count(bq);
		return BQ_CLOSED;
	}

	if (bq->queue_size == 0) {
		if (!bq->get_lock_are_weak_locks_blocked) {
			fair_lock_block_weak_locks(&bq->get_lock);
			bq->get_lock_are_weak_locks_blocked = 1;
		}
		if (async) {
			fair_lock_unlock(&bq->get_lock);
			pthread_mutex_unlock(&bq->mutex);
			decrease_active_callers_count(bq);
			return BQ_EMPTY;
		}
		pthread_cond_wait(&bq->cond, &bq->mutex);
		if (bq->closed) {
			fair_lock_unlock(&bq->get_lock);
			pthread_mutex_unlock(&bq->mutex);
			decrease_active_callers_count(bq);
			return BQ_CLOSED;
		}
		//assert(bq->queue_size >= 1);
	}
	if (bq->add_lock_are_weak_locks_blocked) {
		fair_lock_allow_weak_locks(&bq->add_lock);
		bq->add_lock_are_weak_locks_blocked = 0;
	}
	pthread_cond_signal(&bq->cond);
	*(void**)element = dequeue(bq);
	pthread_mutex_unlock(&bq->mutex);

	fair_lock_unlock(&bq->get_lock);

	decrease_active_callers_count(bq);

	return 0;
}

int blocking_queue_add(Blocking_Queue* bq, void* element) {
	return blocking_queue_add_internal(bq, element, 1);
}

int blocking_queue_put(Blocking_Queue* bq, void* element) {
	return blocking_queue_add_internal(bq, element, 0);
}

int blocking_queue_poll(Blocking_Queue* bq, void* element) {
	return blocking_queue_get_internal(bq, 1, element);
}

int blocking_queue_take(Blocking_Queue* bq, void* element) {
	return blocking_queue_get_internal(bq, 0, element);
}

#endif
#endif
