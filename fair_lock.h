#ifndef C_FEK_FAIR_LOCK
#define C_FEK_FAIR_LOCK

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

	To use this fair lock, define C_FEK_BLOCKING_FAIR_LOCK before including fair_lock.h in one of your source files.

	To use this fair lock, you must link your binary with pthread.

	This fair lock is thread-safe.

	This fair lock is very similar to pthread's fair lock. The difference is that all blocked callers are served in FIFO order.

	This fair lock also provides "weak locks", which are locks that can be given up. Check the API for details.

	Define C_FEK_FAIR_LOCK_QUEUE_NO_CRT if you don't want the C Runtime Library included. If this is defined, you must provide
	implementations for the following functions:

	void* malloc(unsigned int size)
	void  free(void* block)

	For more information about the API, check the comments in the function signatures.

	https://github.com/felipeek/c-fifo-blocking-queue
*/

#include <pthread.h>

#define FL_ERROR 1
#define FL_ABANDONED 2

// This structure is reserved for internal-use only
typedef struct Cond_Queue_Entry {
	pthread_cond_t cond;
	// If true, this entry is bound to a weak lock
	int weak;
	// If true, the lock bound to this entry was abandoned
	int abandoned;
	struct Cond_Queue_Entry* next;
} Cond_Queue_Entry;

// This structure is reserved for internal-use only
typedef struct Fair_Lock {
	// Internal Mutex
	pthread_mutex_t mutex;
	// The first Cond_Queue_Entry in the lock queue
	Cond_Queue_Entry* cond_queue_front;
	// The last Cond_Queue_Entry in the lock queue
	Cond_Queue_Entry* cond_queue_rear;
	// Pool of already-allocated Cond_Queue_Entry structures
	Cond_Queue_Entry* cond_pool;
	// Specifies whether the lock is currently owned by a thread
	int is_lock_acquired;
	// Number of threads waiting for the lock.
	// Threads bound to lock requests that were abandoned, but are still waiting their turn, are not counted.
	int waiting_threads;
	// If true, weak locks should be discarded.
	int block_weak_locks;
} Fair_Lock;

// Initializes the fair lock.
// Returns 0 if success, FL_ERROR otherwise.
int fair_lock_init(Fair_Lock* lock);
// Destroys the fair lock.
// A fair lock can only be destroyed if nobody currently holds the lock.
// After destroyed, the lock cannot be used anymore.
void fair_lock_destroy(Fair_Lock* lock);
// The fair lock is locked.
// If the fair lock is already locked, the calling thread blocks until the fair lock becomes available.
// FIFO order is guaranteed - callers will never suffer from starvation.
// This function may alloc memory. If there is no memory available, it will fail.
// Returns 0 if success, FL_ERROR otherwise.
int fair_lock_lock(Fair_Lock *lock);
// The fair lock is unlocked.
// *Can only be called if the lock is held by the caller*
void fair_lock_unlock(Fair_Lock *lock);
// Auxiliar function that allows the locking of a 'weak' lock.
// Behavior is equal to 'fair_lock_lock', the only difference is that if 'fair_lock_block_weak_locks' is
// called, weak locks will be rejected - this function will return FL_ABANDONED and the lock will NOT be acquired.
// Note that if the caller calls this function when weak locks are not blocked, but then someone blocks weak locks while
// that thread was still blocked waiting for the lock to be acquired, this function will immediately return with FL_ABANDONED,
// even though the call was made when weak locks were allowed.
// Returns 0 if success, FL_ERROR if error and FL_ABANDONED if weak locks were blocked.
int fair_lock_lock_weak(Fair_Lock *lock);
// Block all weak locks. Any lock request blocked in the 'fair_lock_lock_weak' function will be interrupted and the function will
// return FL_ABANDONED, as stated before. Also, new calls to 'fair_lock_lock_weak' will immediately return FL_ABANDONED until
// 'fair_lock_allow_weak_locks' is called.
// If weak locks are already blocked, this call does nothing.
void fair_lock_block_weak_locks(Fair_Lock* lock);
// Allow all weak locks. After this call, weak locks are allowed again and behave normally.
// If weak locks are already allowed, this call does nothing.
void fair_lock_allow_weak_locks(Fair_Lock* lock);

#ifdef C_FEK_FAIR_LOCK_IMPLEMENTATION
#if !defined(C_FEK_FAIR_LOCK_NO_CRT)
#include <stdlib.h>
#endif

static Cond_Queue_Entry* enqueue_cond_queue_entry(Fair_Lock* lock, int weak) {
	if (lock->cond_pool == NULL) {
		Cond_Queue_Entry* new_entry = (Cond_Queue_Entry*)malloc(sizeof(Cond_Queue_Entry));
		if (new_entry == NULL) {
			return NULL;
		}
		if (pthread_cond_init(&new_entry->cond, NULL)) {
			free(new_entry);
			return NULL;
		}
		lock->cond_pool = new_entry;
		lock->cond_pool->next = NULL;
	}

	lock->cond_pool->weak = weak;
	lock->cond_pool->abandoned = 0;
	if (lock->cond_queue_front != NULL) {
		lock->cond_queue_rear->next = lock->cond_pool;
		lock->cond_queue_rear = lock->cond_pool;
	} else {
		lock->cond_queue_front = lock->cond_pool;
		lock->cond_queue_rear = lock->cond_pool;
	}
	
	lock->cond_pool = lock->cond_pool->next;
	lock->cond_queue_rear->next = NULL;

	return lock->cond_queue_rear;
}

static Cond_Queue_Entry* dequeue_cond_queue_entry(Fair_Lock* lock) {
	if (lock->cond_queue_front == NULL) {
		return NULL;
	}

	Cond_Queue_Entry* target = lock->cond_queue_front;

	if (target == lock->cond_queue_rear) {
		//assert(target->next == NULL);
		lock->cond_queue_rear = NULL;
	}

	lock->cond_queue_front = target->next;

	return target;
}

static void release_cond_queue_entry(Fair_Lock* lock, Cond_Queue_Entry* entry) {
	entry->next = lock->cond_pool;
	lock->cond_pool = entry;
}

static void abandone_weak_locks(Fair_Lock* lock) {
	Cond_Queue_Entry* current_entry = lock->cond_queue_front;
	Cond_Queue_Entry* first_strong_entry = NULL;
	Cond_Queue_Entry* last_strong_entry = NULL;
	while (current_entry) {
		if (current_entry->weak) {
			if (last_strong_entry != NULL) {
				last_strong_entry->next = current_entry->next;
			}
			current_entry->abandoned = 1;
			--lock->waiting_threads;
			pthread_cond_signal(&current_entry->cond);
		} else {
			if (first_strong_entry == NULL) {
				first_strong_entry = current_entry;
			}
			last_strong_entry = current_entry;
		}
		current_entry = current_entry->next;
	}
	lock->cond_queue_front = first_strong_entry;
	lock->cond_queue_rear = last_strong_entry;
}

int fair_lock_init(Fair_Lock* lock) {
	if (pthread_mutex_init(&lock->mutex, NULL)) {
		return FL_ERROR;
	}

	lock->cond_pool = NULL;
	lock->cond_queue_front = NULL;
	lock->cond_queue_rear = NULL;
	lock->is_lock_acquired = 0;
	lock->waiting_threads = 0;
	lock->block_weak_locks = 0;

	return 0;
}

void fair_lock_destroy(Fair_Lock* lock) {
	//assert(lock->cond_queue_front == NULL);
	Cond_Queue_Entry* entry = lock->cond_pool;
	while (entry) {
		Cond_Queue_Entry* next = entry->next;
		//assert(!pthread_cond_destroy(&entry->cond));
		free(entry);
		entry = next;
	}
	pthread_mutex_destroy(&lock->mutex);
}

static int _fair_lock_lock(Fair_Lock *lock, int weak) {
	pthread_mutex_lock(&lock->mutex);
	if (weak && lock->block_weak_locks) {
		pthread_mutex_unlock(&lock->mutex);
		return FL_ABANDONED;
	}
	if (lock->is_lock_acquired || lock->waiting_threads > 0) {
		++lock->waiting_threads;
		Cond_Queue_Entry* entry = enqueue_cond_queue_entry(lock, weak);
		if (entry == NULL) {
			--lock->waiting_threads;
			pthread_mutex_unlock(&lock->mutex);
			return FL_ERROR;
		}
		pthread_cond_wait(&entry->cond, &lock->mutex);
		release_cond_queue_entry(lock, entry);
		if (entry->abandoned) {
			//assert(weak);
			pthread_mutex_unlock(&lock->mutex);
			return FL_ABANDONED;
		}
		--lock->waiting_threads;
	}
	//assert(lock->is_lock_acquired == 0);
	lock->is_lock_acquired = 1;
	pthread_mutex_unlock(&lock->mutex);
	return 0;
}

int fair_lock_lock(Fair_Lock *lock) {
	return _fair_lock_lock(lock, 0);
}

int fair_lock_lock_weak(Fair_Lock *lock) {
	return _fair_lock_lock(lock, 1);
}

void fair_lock_unlock(Fair_Lock *lock)
{
	pthread_mutex_lock(&lock->mutex);
	//assert(lock->is_lock_acquired);
	Cond_Queue_Entry* entry = dequeue_cond_queue_entry(lock);
	if (entry != NULL) {
		//assert(entry->abandoned == 0);
		pthread_cond_signal(&entry->cond);
	}
	
	lock->is_lock_acquired = 0;
	pthread_mutex_unlock(&lock->mutex);
}

void fair_lock_block_weak_locks(Fair_Lock* lock)
{
	pthread_mutex_lock(&lock->mutex);
	lock->block_weak_locks = 1;
	abandone_weak_locks(lock);
	pthread_mutex_unlock(&lock->mutex);
}

void fair_lock_allow_weak_locks(Fair_Lock* lock)
{
	pthread_mutex_lock(&lock->mutex);
	lock->block_weak_locks = 0;
	pthread_mutex_unlock(&lock->mutex);
}

#endif
#endif
