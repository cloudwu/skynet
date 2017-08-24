v1.1.0-rc (2017-7-18)
-----------
* config file : support include
* debug console : User config binding IP
* debug console : Add call command
* debug console : Report error message of inject code
* debug console : Change response message
* sharedata : Add sharedata.flush
* sharedata : Add sharedata.deepcopy
* cluster : Add cluster.send
* cluster : Add API to update config table
* skynet : Add skynet.state
* skynet : Keep the order of skynet.wakeup
* skynet : Add a MEMORY_CHECK macro for debugging
* httpc : Add httpc.timeout
* mongo driver : sort support multi-key
* bson : Check utf8 string
* bson : No longer support numberic key 
* daemon mode: Can output the error messages
* sproto : Support decimal number
* sproto: Support binary type
* sproto: Support response nil
* crypt: Add crypt.hmac64_md5
* redis: Add redis-cluster support
* socket server : Optimize socket write (Try direct write from worker thread first)
* Add prefix skynet to all skynet lua modules
* datasheet : New module for replacement of sharedata
* jemalloc : Update to 5.0.1
* lua : Update to 5.3.4
* lpeg : Update to 1.0.1

v1.0.0 (2016-7-11)
-----------
* Version 1.0.0 Released

v1.0.0-rc5 (2016-7-4)
-----------
* MongoDB : Support auth_scram_sha1
* MongoDB : Auto determine primary host
* Bugfix : memory leak in multicast
* Bugfix : Lua 5.3.3
* Bson : support meta array

v1.0.0-rc4 (2016-6-13)
-----------
* Update lua to 5.3.3
* Update jemalloc to 4.2.1
* Add debug console command ping
* Lua bson support __pairs
* Add mongo.createIndexes and fix bug in old mongo.createIndex
* Handle signal HUP to reopen log file (for logrotate)

v1.0.0-rc3 (2016-5-9)
-----------
* Update jemalloc 4.1.1
* Update lua 5.3.3 rc1
* Update sproto to support encoding empty table
* Make skynet.init stable (keep order)
* skynet.getenv can return empty string
* Add lua VM memory warning
* lua VM support memory limit
* skynet.pcall suport varargs
* Bugfix : Global name query
* Bugfix : snax.queryglobal

v1.0.0-rc2 (2016-3-7)
-----------
* Fix a bug in lua 5.3.2
* Update sproto (fix bugs and add ud for package)
* Fix a bug in http
* Fix a bug in harbor
* Fix a bug in socket channel
* Enhance remote debugger

v1.0.0-rc (2015-12-28)
-----------
* Update to lua 5.3.2
* Add skynet.coroutine lib
* Add new debug api to show c memory used
* httpc can use async dns query
* Redis driver support pipeline
* socket.send support string table, and rewrite redis driver
* socket.shutdown would abandon unsend buffer
* Improve some sproto api
* c memory doesn't count the memory allocated by lua vm
* some other bugfix (In multicast, socketchannel, etc)

v1.0.0-beta (2015-11-10)
-----------
* Improve and fix bug for sproto
* Add global short string pool for lua vm
* Add code cache mode
* Add a callback for mysql auth
* Add hmac_md5
* Sharedata support filename as a string
* Fix a bug in socket.httpc
* Fix a lua stack overflow bug in lua bson
* Fix a socketchannel bug may block the data steam
* Avoid dead loop when sending message to the service exiting
* Fix memory leak in netpack
* Improve DH key exchange implement
* Minor fix for socket
* Minor fix for multicast
* Update jemalloc to 4.0.4
* Update lpeg to 1.0.0

v1.0.0-alpha10 (2015-8-17)
-----------
* Remove the size limit of cluster RPC message.
* Remove the size limit of local message.
* Add cluster.query and clsuter.register.
* Add an option of pthread mutex lock.
* Add skynet.core.intcommand to optimize skynet.sleep etc.
* Fix a memory leak bug in lua shared proto.
* snax.msgserver use string instead of lightuserdata/size.
* Remove some unused api in netpack.
* Raise error when skynet.send to 0.

v1.0.0-alpha9 (2015-8-10)
-----------
* Improve lua serialization , support pairs metamethod.
* Bugfix : sproto (See commits log of sproto)
* Add user log service support (In config)
* Other minor bugfix (See commits log)

v1.0.0-alpha8 (2015-6-29)
-----------
* Update lua 5.3.1
* Bugfix: skynet exit issue
* Bugfix: timer race condition
* Use atom increment in bson object id
* remove assert when write to a listen fd
* sproto encode doesn't use raw table api

v1.0.0-alpha7 (2015-6-8)
-----------
* console support launch snax service
* Add cluster.snax
* Add nodelay in clusterd
* Merge sproto bugfix patch
* Move some skynet api into skynet.manager
* DNS support underscore
* Add logservice in config file for user defined log service
* skynet.fork returns coroutine
* Fix a few of bugs , see the commits log

v1.0.0-alpha6 (2015-5-18)
-----------
* bugfix: httpc.get
* bugfix: seri lib stack overflow
* bugfix: udp send
* bugfix: udp address
* bugfix: sproto dump
* add: sproto default
* improve: skynet.wakeup (can wakeup skynet.call by raise an error)
* improve: skynet.exit (raise error when uncall response)
* remove: task overload warning
* move: some skynet api move into skynet.manager

v1.0.0-alpha5 (2015-4-27)
-----------
* merge lua 5.3 offical bugfix 
* improve sproto rpc api
* fix a deadlock bug when service retire
* improve cluster config reload
* add skynet.pcall for calling a function with `require`
* better error log in loginserver

v1.0.0-alpha4 (2015-4-13)
-----------
* sproto can share c struct between states
* udp api changed (use lua string now)
* fix memory leak in dns module

v1.0.0-alpha3 (2015-3-30)
-----------
* Update sproto (bugfix)
* Add async dns query
* improve httpc

v1.0.0-alpha2 (2015-3-16)
-----------
* Update examples client to lua 5.3
* Patch lua 5.3 to interrupt the dead loop (for debug)
* Update sproto (fix some bugs and support unordered map)

v1.0.0-alpha (2015-3-9)
-----------
* Update lua from 5.2 to 5.3
* Add an online lua debugger
* Add sharemap as an example use case of stm
* Improve sproto for multi-state
* Improve mongodb driver
* Fix known bugs

v0.9.3 (2015-1-5)
-----------
* Add : mongo createIndex
* Update : sproto
* bugfix : sharedata check dirty flag when len/pairs metamethod
* bugfix : multicast

v0.9.2 (2014-12-8)
-----------
* Simplify the message queue
* Add create_index in mongo driver
* Fix a bug in big-endian architecture (sproto)

v0.9.0 / v0.9.1 (2014-11-17)
-----------
* Add UDP support
* Add IPv6 support
* socket send package can define a release method
* dispatch read before write in epoll
* remove snax queue mode
* Fix a bug in big-endian architecture

v0.8.1 (2014-11-3)
-----------
* Send to an invalid remote service will raise an error
* Bugifx: socket open address string
* Remove sha1 from mysqlaux
* merge lua and sproto bugfix , use crypt lib instead
* Fix a memory leak in socket
* minor bugfix in http module

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
