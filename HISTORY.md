Dev 
===

Major Changes

* Rewrite malloc hook , use `pthread_getspecific` instead of `__thread` to get current service handle.
* Optimize global unique service query, rewrite `service_mgr` .
* Add some snax api, snax.uniqueservice (etc.) , use independent protocol `PTYPE_SNAX` .
* Add bootstrap lua script , remove some code in C .
* Use a lua loader to load lua service code (and set the lua environment), remove some code in C.
* Support preload a file before each lua serivce start.
* Add datacenter serivce.
* Add multicast api.

v0.1.1 (2014-4-28)
======

Major Changes

* socket channel reconnect should clear request queue.
* socket close may block the coroutine.
* jemalloc api may crash on macosx (disable jemalloc on macosx).

v0.1.0 (2014-4-23)
======

First public version (2012-8-1)
======
