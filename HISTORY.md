v0.2.1 (2014-5-19)
-----------
* Bugfix: check all the events already read after socket close
* Bugfix: socket data in gate service 
* Bugfix: boundary problem in harbor service
* Bugfix: stdin handle is 0

v0.2.0 (2014-5-12)
-----------

* Rewrite malloc hook , use `pthread_getspecific` instead of `__thread` to get current service handle.
* Optimize global unique service query, rewrite `service_mgr` .
* Add some snax api, snax.uniqueservice (etc.) , use independent protocol `PTYPE_SNAX` .
* Add bootstrap lua script , remove some code in C .
* Use a lua loader to load lua service code (and set the lua environment), remove some code in C.
* Support preload a file before each lua serivce start.
* Add datacenter serivce.
* Add multicast api.
* Remove skynet.blockcall , simplify the implement of message queue.
* When dropping message queue (at service exit) , dispatcher will post an error back to the source of each message.
* Remove skynet.watch , monitor is not necessary for watching skynet.call . so simplemonitor.lua is move to examples.
* Remove the limit of global queue size (64K actived service limit before).
* Refactoring `skynet_command`.

v0.1.1 (2014-4-28)
------------------

* Socket channel should clear request queue when reconnect.
* Fix the issue that socket close may block the coroutine.
* Fix the issue that jemalloc api may crash on macosx (disable jemalloc on macosx).

v0.1.0 (2014-4-23)
------------------

* First release version.

First public version (2012-8-1)
------------------

* Make skynet from a private project to public.
