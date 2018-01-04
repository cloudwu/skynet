#ifndef SKYNET_IMP_H
#define SKYNET_IMP_H

struct skynet_config {
	int thread;   //启动工作线程数量，不要配置超过实际拥有的CPU核心数
	// skynet网络节点的唯一编号，可以是 1-255 间的任意整数。
	// 一个 skynet 网络最多支持 255 个节点。每个节点有必须有一个唯一的编号。
	// 如果 harbor 为 0 ，skynet 工作在单节点模式下。此时 master 和 address 以及 standalone 都不必设置。
	int harbor;
	int profile;             //是否开启统计功能，统计每个服务使用了多少cpu时间，默认开启
	const char * daemon;     //后台模式：daemon = "./skynet.pid"可以以后台模式启动skynet（注意，同时请配置logger 项输出log）
	const char * module_path;//用 C 编写的服务模块的位置，通常指 cservice 下那些 .so 文件
	const char * bootstrap;  //skynet 启动的第一个服务以及其启动参数。默认配置为 snlua bootstrap ，即启动一个名为 bootstrap 的 lua 服务。通常指的是 service/bootstrap.lua 这段代码。
	const char * logger;     //它决定了 skynet 内建的 skynet_error 这个 C API 将信息输出到什么文件中。如果 logger 配置为 nil ，将输出到标准输出。你可以配置一个文件名来将信息记录在特定文件中。
	const char * logservice; //默认为 "logger" ，你可以配置为你定制的 log 服务（比如加上时间戳等更多信息）。可以参考 service_logger.c 来实现它。注：如果你希望用 lua 来编写这个服务，可以在这里填写 snlua ，然后在 logger 配置具体的 lua 服务的名字。在 examples 目录下，有 config.userlog 这个范例可供参考。
};

#define THREAD_WORKER 0
#define THREAD_MAIN 1
#define THREAD_SOCKET 2
#define THREAD_TIMER 3
#define THREAD_MONITOR 4

void skynet_start(struct skynet_config * config);

#endif
