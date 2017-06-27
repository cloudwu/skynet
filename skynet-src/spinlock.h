#ifndef SKYNET_SPINLOCK_H
#define SKYNET_SPINLOCK_H

#include <assert.h>

#define SPIN_INIT(q) spinlock_init(&(q)->lock);
#define SPIN_LOCK(q) spinlock_lock(&(q)->lock);
#define SPIN_UNLOCK(q) spinlock_unlock(&(q)->lock);
#define SPIN_DESTROY(q) spinlock_destroy(&(q)->lock);

#ifndef USE_PTHREAD_LOCK

struct spinlock {
	int lock;
};

static inline void
spinlock_init(struct spinlock *lock) {
	lock->lock = 0;
}

static inline void
spinlock_lock(struct spinlock *lock) {
	while (__sync_lock_test_and_set(&lock->lock,1)) {}
}

static inline int
spinlock_trylock(struct spinlock *lock) {
	return __sync_lock_test_and_set(&lock->lock,1) == 0;
}

static inline void
spinlock_unlock(struct spinlock *lock) {
	__sync_lock_release(&lock->lock);
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

struct spinlock_nested {
	struct spinlock lock;
	int thread;
	int count;
};

static inline void
spinlock_nestedinit(struct spinlock_nested *ln) {
	spinlock_init(&ln->lock);
	ln->thread = 0;
	ln->count = 0;
}

static inline void
spinlock_nestedlock(struct spinlock_nested *ln, int thread) {
	if (thread != ln->thread) {
		spinlock_lock(&ln->lock);
		assert(ln->count == 0);
		ln->thread = thread;
	}
	++ln->count;
}

static inline int
spinlock_nestedtrylock(struct spinlock_nested *ln, int thread) {
	if (thread != ln->thread) {
		if (spinlock_trylock(&ln->lock)) {
			assert(ln->count == 0);
			ln->thread = thread;
			++ln->count;
			return 1;	// lock succ
		} else {
			return 0;
		}
	} else {
		// in the same thread
		++ln->count;
		return 1;
	}
}

static inline void
spinlock_nestedunlock(struct spinlock_nested *ln, int thread) {
	assert(thread == ln->thread);
	--ln->count;
	if (ln->count > 0) {
		return;
	}
	assert(ln->count == 0);
	ln->thread = 0;
	spinlock_unlock(&ln->lock);
}

static inline void
spinlock_nesteddestroy(struct spinlock_nested *ln) {
	assert(ln->thread == 0 && ln->count == 0);
	spinlock_destroy(&ln->lock);
}

#endif
