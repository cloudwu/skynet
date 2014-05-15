#ifndef SKYNET_IMP_H
#define SKYNET_IMP_H

// skynet 的配置
struct skynet_config {
	int thread; 			  // 线程数
	int harbor; 			  // harbor
	const char * logger; 	  // 日志服务
	const char * module_path; // 模块 即服务的路径 .so文件路径
	const char * master;	  // master服务
	const char * local;       // 本地ip和port
	const char * start;       //
	const char * standalone;  // 是否是单机版
};

void skynet_start(struct skynet_config * config);

#endif
