#ifndef SKYNET_RWLOCK_H
#define SKYNET_RWLOCK_H

// 读写锁的数据结构
struct rwlock {
	int write;		// 写的计数统计
	int read;		// 读的计数统计
};

/**
 * 初始化
 * @param lock rwlock 数据结构
 */
static inline void
rwlock_init(struct rwlock *lock) {
	lock->write = 0;
	lock->read = 0;
}

/**
 * <读> 操作上锁
 * @param lock rwlock 数据结构
 */
static inline void
rwlock_rlock(struct rwlock *lock) {
	for (;;) {

		// 如果有其他的线程正在 [写], 那么一直等着, 等待其他线程的 [写] 操作执行完毕.
		while(lock->write) {
			__sync_synchronize();
		}

		// <读> 计数 +1, <读> 锁使用 +1 的操作, 是因为可以多个线程同时读.
		__sync_add_and_fetch(&lock->read,1);

		// 如果这时又有其他线程进行 [写] 操作, 重置 <读> 的引用计数, 让其他线程先进行完 [写] 操作, 
		// 当前线程一直等待, 直到其他线程的 [写] 操作执行完毕, 才跳出循环.
		if (lock->write) {
			__sync_sub_and_fetch(&lock->read,1);
		} else {
			break;
		}
	}
}

/**
 * [写] 操作上锁
 * @param lock rwlock 数据结构
 */
static inline void
rwlock_wlock(struct rwlock *lock) {
	
	// 保证没有其他线程当前正在进行 [写] 操作, 当前将 [写] 操作的计数记为 1.
	// 只能保证当前只有一个线程在执行 [写] 操作.
	while (__sync_lock_test_and_set(&lock->write,1)) {}

	// 如果这时有其他线程正在 <读>, 那么等待其他线程的释放 <读> 的锁后, 再开始当前线程的 [写] 操作.
	while(lock->read) {
		__sync_synchronize();
	}
}

/**
 * 释放 [写] 操作锁
 * @param lock rwlock 数据结构
 */
static inline void
rwlock_wunlock(struct rwlock *lock) {
	__sync_lock_release(&lock->write);
}

/**
 * 释放 <读> 操作锁
 * @param lock rwlock 数据结构
 */
static inline void
rwlock_runlock(struct rwlock *lock) {
	__sync_sub_and_fetch(&lock->read,1);
}

#endif
