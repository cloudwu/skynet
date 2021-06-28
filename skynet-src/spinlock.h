#ifndef SKYNET_SPINLOCK_H
#define SKYNET_SPINLOCK_H

#define SPIN_INIT(q) spinlock_init(&(q)->lock);
#define SPIN_LOCK(q) spinlock_lock(&(q)->lock);
#define SPIN_UNLOCK(q) spinlock_unlock(&(q)->lock);
#define SPIN_DESTROY(q) spinlock_destroy(&(q)->lock);

#ifndef USE_PTHREAD_LOCK

#ifdef __STDC_NO_ATOMICS__

#define atomic_int int
#define atomic_test_and_set_(ptr) __atomic_exchange_n(ptr, 1, __ATOMIC_ACQUIRE)
#define atomic_clear_(ptr) __atomic_store_n(ptr, 0, __ATOMIC_RELEASE)
#define atomic_load_relaxed_(ptr) __atomic_load_n(ptr, __ATOMIC_RELAXED)
#else

#include <stdatomic.h>
#define atomic_test_and_set_(ptr) atomic_exchange_explicit(ptr, 1, memory_order_acquire)
#define atomic_clear_(ptr) atomic_store_explicit(ptr, 0, memory_order_release);
#define atomic_load_relaxed_(ptr) atomic_load_explicit(ptr, memory_order_relaxed)
#endif

#if defined(__x86_64__)
#define atomic_pause_() __builtin_ia32_pause()
#else
#define atomic_pause_() ((void)0)
#endif

struct spinlock {
	atomic_int lock;
};

static inline void
spinlock_init(struct spinlock *lock) {
	lock->lock = 0;
}

static inline void
spinlock_lock(struct spinlock *lock) {
	for (;;) {
	  if (!atomic_test_and_set_(&lock->lock))
		return;
	  while (atomic_load_relaxed_(&lock->lock))
		atomic_pause_();
	}
}

static inline int
spinlock_trylock(struct spinlock *lock) {
    return !atomic_load_relaxed_(&lock->lock) &&
           !atomic_test_and_set_(&lock->lock);
}

static inline void
spinlock_unlock(struct spinlock *lock) {
	atomic_clear_(&lock->lock);
}

static inline void
spinlock_destroy(struct spinlock *lock) {
	(void) lock;
}

#else

#include <pthread.h>

// we use mutex instead of spinlock for some reason
// you can also replace to pthread_spinlock

struct spinlock {
	pthread_mutex_t lock;
};

static inline void
spinlock_init(struct spinlock *lock) {
	pthread_mutex_init(&lock->lock, NULL);
}

static inline void
spinlock_lock(struct spinlock *lock) {
	pthread_mutex_lock(&lock->lock);
}

static inline int
spinlock_trylock(struct spinlock *lock) {
	return pthread_mutex_trylock(&lock->lock) == 0;
}

static inline void
spinlock_unlock(struct spinlock *lock) {
	pthread_mutex_unlock(&lock->lock);
}

static inline void
spinlock_destroy(struct spinlock *lock) {
	pthread_mutex_destroy(&lock->lock);
}

#endif

#endif
