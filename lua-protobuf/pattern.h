#ifndef PROTOBUF_C_PATTERN_H
#define PROTOBUF_C_PATTERN_H

#include "pbc.h"
#include "context.h"
#include "array.h"

struct _pattern_field {
	int id;
	int offset;
	int ptype;
	int ctype;
	int label;
	pbc_var defv;
};

struct pbc_pattern {
	struct pbc_env * env;
	int count;
	struct _pattern_field f[1];
};

struct pbc_pattern * _pbcP_new(struct pbc_env * env, int n);
int _pbcP_unpack_packed(uint8_t *buffer, int size, int ptype, pbc_array array);

#endif
