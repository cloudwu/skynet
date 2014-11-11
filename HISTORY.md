v0.8.0 (2014-10-27)
-----------
* Add mysql client driver
* Bugfix : skynet.queue

v0.7.4 (2014-10-13)
-----------
* Bugfix : clear coroutine pool when GC
* hotfix : A bug introduce by 0.7.3 

v0.7.3 (2014-10-13)
-----------
* Add some logs (warning) when overload
* Bugfix: crash on exit

v0.7.2 (2014-9-29)
-----------
* Bugfix : datacenter.wait
* Bugfix : error in forker coroutine
* Add skynet.term
* Accept socket report port
* sharedata can be update more than once

v0.7.1 (2014-9-22)
-----------
* bugfix: wakeup sleep should return BREAK
* bugfix: sharedatad load string
* bugfix: dataserver forward error msg

v0.7.0 (2014-9-8)
-----------
* Use sproto instead of cjson
* Add message logger
* Add hmac-sha1
* Some minor bugfix

v0.6.2 (2014-9-1)
-----------
* bugfix: only skynet.call response PTYPE_ERROR

v0.6.1 (2014-8-25)
-----------
* bugfix: datacenter.wakeup
* change struct msg name to avoid conflict in mac
* improve seri library

v0.6.0 (2014-8-18)
-----------
* add sharedata
* bugfix: service exit before init would not report back
* add skynet.response and check multicall skynet.ret
* skynet.newservice throw error when lanuch faild
* Don't check imported function in snax.hotfix
* snax service add change SERVICE_PATH and add it to package.path
* skynet.redirect support string address
* bugfix: skynet.harbor.link may block
* add skynet.harbor.queryname to query globalname
* add cluster.proxy 
* add DEBUG command exit (send a message to lua service by DEBUG)
* add DEBUG command run (debug_console command inject)
* bugfix : socketchannel connect once
* bugfix : mongo driver

v0.5.2 (2014-8-11)
-----------
* Bugfix : httpd request
* Bugifx : http chunked mode
* Add : httpc
* timer support more than 497 days

v0.5.1 (2014-8-4)
-----------
* Bugfix : http module
* Bugfix : multicast local channel delete
* Bugfix : socket.read(fd)

v0.5.0 (2014-7-28)
-----------
* skynet.exit will quit service immediately.
* Add snax.gateserver, snax.loginserver, snax.msgserver
* Simplify clientsocket lib
* mongo driver support replica set
* config file support read from ENV
* add simple httpd (see examples/simpleweb.lua)

v0.4.2 (2014-7-14)
-----------
* Bugfix : invalid negative socket id 
* Add optional TCP_NODELAY support
* Add worker thread weight
* Add skynet.queue
* Bugfix: socketchannel
* cluster can throw error
* Add readline and writeline to clientsocket lib
* Add cluster.reload to reload config file
* Add datacenter.wait

v0.4.1 (2014-7-7)
-----------
* Add SERVICE_NAME in loader
* Throw error back when skynet.error
* Add skynet.task
* Bugfix for last version (harbor service bugs)

v0.4.0 (2014-6-30)
-----------
* Optimize redis driver `compose_message`.
* Add module skynet.harbor for monitor harbor connect/disconnect, see test/testharborlink.lua .
* cluster.open support cluster name.
* Add new api skynet.packstring , and skynet.unpack support lua string
* socket.listen support put port into address. (address:port)
* Redesign harbor/master/dummy, remove lots of C code and rewite in lua.
* Remove block connect api, queue sending message during connecting now.
* Add skynet.time()

v0.3.2 (2014-6-23)
----------
* Bugfix : cluster (double free).
* Add socket.header() to decode big-endian package header (and fix the bug in cluster).

v0.3.1 (2014-6-16)
-----------
* Bugfix: lua mongo driver . Hold reply string before decode bson data.
* More check in bson decoding.
* Use big-endian for encoding bson objectid.

v0.3.0 (2014-6-2)
-----------
* Add cluster support
* Add single node mode
* Add daemon mode
* Bugfix: update lua-bson (signed 32bit int bug / check string length)
* Optimize timer
* Simplify message queue and optimize message dispatch
* Use jemalloc release 3.6.0

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
