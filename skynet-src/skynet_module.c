#include "skynet_module.h"

#include <assert.h>
#include <string.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

// 动态链接库 .so 的加载

// 模块类型最大32,不代表模块所对应的服务最大数是32 一种模块可以对应多个服务

#define MAX_MODULE_TYPE 32

struct modules {
	int count;			// 已加载的模块总数
	int lock;			// 用于原子操作
	const char * path;	// 模块所在路径
	struct skynet_module m[MAX_MODULE_TYPE];	// 模块数组
};

static struct modules * M = NULL;

// 尝试打开一个模块
static void *
_try_open(struct modules *m, const char * name) {
	const char *l;
	const char * path = m->path;
	size_t path_size = strlen(path);
	size_t name_size = strlen(name);

	int sz = path_size + name_size;
	//search path
	void * dl = NULL;
	char tmp[sz];
	do
	{
		memset(tmp,0,sz);
		while (*path == ';') path++;
		if (*path == '\0') break;

		l = strchr(path, ';');
		if (l == NULL) l = path + strlen(path);

		int len = l - path;
		int i;
		for (i=0; path[i]!='?' && i < len ;i++) {
			tmp[i] = path[i];
		}

		memcpy(tmp+i,name,name_size);
		if (path[i] == '?') {
			strncpy(tmp+i+name_size,path+i+1,len - i - 1);
		} else {
			fprintf(stderr,"Invalid C service path\n");
			exit(1);
		}

		dl = dlopen(tmp, RTLD_NOW | RTLD_GLOBAL); // dlopen() 打开一个动态链接库，并返回动态链接库的句柄
		path = l;
	}while(dl == NULL);

	if (dl == NULL) {
		fprintf(stderr, "try open %s failed : %s\n",name,dlerror());
	}

	return dl;
}

static struct skynet_module * 
_query(const char * name) {
	int i;
	for (i=0;i<M->count;i++) {
		if (strcmp(M->m[i].name,name)==0) {
			return &M->m[i];
		}
	}
	return NULL;
}

// 初始化xxx_create、xxx_init、xxx_release地址
// 成功返回0，失败返回1
static int
_open_sym(struct skynet_module *mod) {
	size_t name_size = strlen(mod->name);
	char tmp[name_size + 9]; // create/init/release , longest name is release (7)
	memcpy(tmp, mod->name, name_size);

	strcpy(tmp+name_size, "_create");

	mod->create = dlsym(mod->module, tmp);
	strcpy(tmp+name_size, "_init");

	mod->init = dlsym(mod->module, tmp);
	strcpy(tmp+name_size, "_release");

	mod->release = dlsym(mod->module, tmp); // dlsym() 根据动态链接库操作句柄与符号，返回符号对应的地址。

	return mod->init == NULL;
}

// 根据名称查找模块，如果没有找到，则加载该模块
struct skynet_module * 
skynet_module_query(const char * name) {
	struct skynet_module * result = _query(name);	// 查找模块
	if (result)
		return result;

	while(__sync_lock_test_and_set(&M->lock,1)) {}

	result = _query(name); // double check

	if (result == NULL && M->count < MAX_MODULE_TYPE) { // 如果没有这个模块 测试打开这个模块
		int index = M->count;
		void * dl = _try_open(M,name);		// 加载动态链接库
		if (dl) {
			M->m[index].name = name;
			M->m[index].module = dl;

			// 初始化xxx_create xxx_init xxx_release地址
			if (_open_sym(&M->m[index]) == 0) {
				M->m[index].name = strdup(name);
				M->count ++;
				result = &M->m[index];
			}
		}
	}

	__sync_lock_release(&M->lock);

	return result;
}

void 
skynet_module_insert(struct skynet_module *mod) {
	while(__sync_lock_test_and_set(&M->lock,1)) {}

	struct skynet_module * m = _query(mod->name);
	assert(m == NULL && M->count < MAX_MODULE_TYPE); // 不存在这个模块
	int index = M->count;
	M->m[index] = *mod; // 将这个模块放到对应的slot中
	++M->count;
	__sync_lock_release(&M->lock);
}

void * 
skynet_module_instance_create(struct skynet_module *m) {
	if (m->create) {
		return m->create();
	} else {
		return (void *)(intptr_t)(~0);	// intptr_t对于32位环境是int，对于64位环境是long int
										// C99规定intptr_t可以保存指针值，因而将(~0)先转为intptr_t再转为void*
	}
}

int
skynet_module_instance_init(struct skynet_module *m, void * inst, struct skynet_context *ctx, const char * parm) {
	return m->init(inst, ctx, parm);
}

void 
skynet_module_instance_release(struct skynet_module *m, void *inst) {
	if (m->release) {
		m->release(inst);
	}
}

void 
skynet_module_init(const char *path) {
	struct modules *m = malloc(sizeof(*m));
	m->count = 0;
	m->path = strdup(path);		// 初始化模块所在路径
	m->lock = 0;

	M = m;
}
