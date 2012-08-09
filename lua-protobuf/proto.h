#ifndef PROTOBUFC_PROTO_H
#define PROTOBUFC_PROTO_H

#include "pbc.h"
#include "map.h"
#include "array.h"

#include <stdbool.h>
#include <stddef.h>

struct map_ip;
struct map_si;
struct map_sp;
struct _message;
struct _enum;

#define LABEL_OPTIONAL 0
#define LABEL_REQUIRED 1
#define LABEL_REPEATED 2
#define LABEL_PACKED 3

struct _field {
	int id;
	const char *name;
	int type;
	int label;
	pbc_var default_v;
	union {
		const char * n;
		struct _message * m;
		struct _enum * e;
	} type_name;
};

struct _message {
	const char * key;
	struct map_ip * id;	// id -> _field
	struct map_sp * name;	// string -> _field
	struct pbc_rmessage * def;	// default message
	struct pbc_env * env;
};

struct _enum {
	const char * key;
	struct map_ip * id;
	struct map_si * name;
	pbc_var default_v;
};

struct pbc_env {
	struct map_sp * files;	// string -> void *
	struct map_sp * enums;	// string -> _enum
	struct map_sp * msgs;	// string -> _message
	const char * lasterror;
};

struct _message * _pbcP_init_message(struct pbc_env * p, const char *name);
void _pbcP_push_message(struct pbc_env * p, const char *name, struct _field *f , pbc_array queue);
struct _enum * _pbcP_push_enum(struct pbc_env * p, const char *name, struct map_kv *table, int sz );
int _pbcP_message_default(struct _message * m, const char * name, pbc_var defv);
struct _message * _pbcP_get_message(struct pbc_env * p, const char *name);
int _pbcP_type(struct _field * field, const char **type);

#endif
