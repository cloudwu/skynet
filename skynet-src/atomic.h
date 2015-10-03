#ifndef SKYNET_ATOMIC_H
#define SKYNET_ATOMIC_H

#ifdef __TINYC__

#define __sync_bool_compare_and_swap(ptr, oval, nval)  (*ptr == oval ? (*ptr = nval , true) : (false))

#define __sync_add_and_fetch(ptr, v) (*ptr += v, *ptr)

#define __sync_and_and_fetch(ptr, v) (*ptr &= v, *ptr)

#define __sync_sub_and_fetch(ptr, v) (*ptr -= v, *ptr)

#define __sync_fetch_and_add_one(ptr) ((*ptr) ++)

#define __sync_lock_test_and_set()

#endif //__TINYC__

#define ATOM_CAS(ptr, oval, nval) __sync_bool_compare_and_swap(ptr, oval, nval)
#define ATOM_CAS_POINTER(ptr, oval, nval) __sync_bool_compare_and_swap(ptr, oval, nval)
#define ATOM_INC(ptr) __sync_add_and_fetch(ptr, 1)

#ifdef __TINYC__
#	define ATOM_FINC(ptr) __sync_fetch_and_add_one(ptr)
#else
#	define ATOM_FINC(ptr) __sync_fetch_and_add(ptr, 1)
#endif

#define ATOM_DEC(ptr) __sync_sub_and_fetch(ptr, 1)
#define ATOM_FDEC(ptr) __sync_fetch_and_sub(ptr, 1)
#define ATOM_ADD(ptr,n) __sync_add_and_fetch(ptr, n)
#define ATOM_SUB(ptr,n) __sync_sub_and_fetch(ptr, n)
#define ATOM_AND(ptr,n) __sync_and_and_fetch(ptr, n)

#endif

