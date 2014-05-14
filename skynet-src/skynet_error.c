#include "skynet.h"
#include "skynet_handle.h"
#include "skynet_mq.h"
#include "skynet_server.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MESSAGE_SIZE 256 // 日志的大小

// skynet 对错误处理的封装

void 
skynet_error(struct skynet_context * context, const char *msg, ...) {
	static uint32_t logger = 0;
	if (logger == 0) {
		logger = skynet_handle_findname("logger"); // 根据名称查找handle 查找服务
	}
	if (logger == 0) {
		return;
	}

	char tmp[LOG_MESSAGE_SIZE];
	char *data = NULL;

	va_list ap; // 可变参数

	va_start(ap,msg);
	int len = vsnprintf(tmp, LOG_MESSAGE_SIZE, msg, ap); // vsnprintf() 将可变参数格式化输出到一个字符数组。
	va_end(ap);

	if (len < LOG_MESSAGE_SIZE) {
		data = strdup(tmp); // strdup() 将串拷贝到新建的位置处 得到实际的 msg
	} else {
		int max_size = LOG_MESSAGE_SIZE;
		for (;;) {
			max_size *= 2;
			data = malloc(max_size); // msg 大于 LOG_MESSAGE_SIZE 尝试 分配更大的空间来存放

			va_start(ap,msg);
			len = vsnprintf(data, max_size, msg, ap); // 将 msg 格式化到 data 中
			va_end(ap);

			if (len < max_size) { // 知道写入  data 的数据不比 max_size 大 实际上就是 data能存放msg
				break;
			}
			free(data);
		}
	}


	struct skynet_message smsg;
	if (context == NULL) {
		smsg.source = 0;
	} else {
		smsg.source = skynet_context_handle(context); // ctx->handle;
	}
	smsg.session = 0;
	smsg.data = data;
	smsg.sz = len | (PTYPE_TEXT << HANDLE_REMOTE_SHIFT);
	skynet_context_push(logger, &smsg); // 将消息发送到对应的 handle 中处理
}

