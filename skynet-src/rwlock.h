#ifndef SKYNET_RWLOCK_H
#define SKYNET_RWLOCK_H

#ifndef USE_PTHREAD_LOCK

#include "atomic.h"

struct rwlock {
	ATOM_INT write;
	ATOM_INT read;
};

static inline void
rwlock_init(struct rwlock *lock) {
	ATOM_INIT(&lock->write, 0);
	ATOM_INIT(&lock->read, 0);
}

static inline void
rwlock_rlock(struct rwlock *lock) {
	for (;;) {
		while(ATOM_LOAD(&lock->write)) {}
		ATOM_FINC(&lock->read);
		if (ATOM_LOAD(&lock->write)) {
			ATOM_FDEC(&lock->read);
		} else {
			break;
		}
	}
}

static inline void
rwlock_wlock(struct rwlock *lock) {
	int clear = 0;
	while (!ATOM_CAS(&lock->write,clear,1)) {}
	while(ATOM_LOAD(&lock->read)) {}
}

static inline void
rwlock_wunlock(struct rwlock *lock) {
	ATOM_STORE(&lock->write, 0);
}

static inline void
rwlock_runlock(struct rwlock *lock) {
	ATOM_FDEC(&lock->read);
}

#else

#include <pthread.h>

// only for some platform doesn't have __sync_*
// todo: check the result of pthread api

struct rwlock {
	pthread_rwlock_t lock;
};

static inline void
rwlock_init(struct rwlock *lock) {
	pthread_rwlock_init(&lock->lock, NULL);
}

static inline void
rwlock_rlock(struct rwlock *lock) {
	 pthread_rwlock_rdlock(&lock->lock);
}

static inline void
rwlock_wlock(struct rwlock *lock) {
	 pthread_rwlock_wrlock(&lock->lock);
}

static inline void
rwlock_wunlock(struct rwlock *lock) {
	pthread_rwlock_unlock(&lock->lock);
}

static inline void
rwlock_runlock(struct rwlock *lock) {
	pthread_rwlock_unlock(&lock->lock);
}

#endif

#endif
