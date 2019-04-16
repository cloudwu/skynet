#ifndef LUA_SHORT_STRING_TABLE_H
#define LUA_SHORT_STRING_TABLE_H

#include "lstring.h"

// If you use modified lua, this macro would be defined in lstring.h
#ifndef ENABLE_SHORT_STRING_TABLE

struct ssm_info {
	int total;
	int longest;
	int slots;
	size_t size;
	double variance;
};

struct ssm_collect {
	void *key;
	int n;
};

static inline void luaS_initssm();
static inline void luaS_exitssm();
static inline void luaS_infossm(struct ssm_info *info) {}
static inline int luaS_collectssm(struct ssm_collect *info) { return 0; }

#endif

#endif
