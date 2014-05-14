#ifndef _RWLOCK_H_
#define _RWLOCK_H_

// 读写锁的翻转
struct rwlock {
	int write;
	int read;
};

static inline void
rwlock_init(struct rwlock *lock) {
	lock->write = 0;
	lock->read = 0;
}

static inline void
rwlock_rlock(struct rwlock *lock) {
	for (;;) {
		while(lock->write) { // 等待写完 再读
			__sync_synchronize();
		}
		__sync_add_and_fetch(&lock->read,1); // 读锁
		if (lock->write) { // 如果在写 释放读锁
			__sync_sub_and_fetch(&lock->read,1);
		} else {
			break;
		}
	}
}

static inline void
rwlock_wlock(struct rwlock *lock) {
	while (__sync_lock_test_and_set(&lock->write,1)) {} // 尝试加写锁
	while(lock->read) { // 如果在读 先等待读完
		__sync_synchronize();
	}
}

static inline void
rwlock_wunlock(struct rwlock *lock) {
	__sync_lock_release(&lock->write);
}

static inline void
rwlock_runlock(struct rwlock *lock) {
	__sync_sub_and_fetch(&lock->read,1);
}

#endif
