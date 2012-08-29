#include "pbc.h"
#include "proto.h"
#include "alloc.h"
#include "map.h"
#include "bootstrap.h"
#include "context.h"
#include "stringpool.h"

#include <string.h>
#include <stdlib.h>

static const char *
_concat_name(struct _stringpool *p , const char *prefix ,  int prefix_sz , const char *name , int name_sz, int *sz) {
	if (prefix_sz == 0) {
		if (sz) {
			*sz = name_sz;
		}
		return _pbcS_build(p , name, name_sz);
	}
	char temp[name_sz + prefix_sz + 2];
	memcpy(temp,prefix,prefix_sz);
	temp[prefix_sz] = '.';
	memcpy(temp+prefix_sz+1,name,name_sz);
	temp[name_sz + prefix_sz + 1] = '\0';
	if (sz) {
		*sz = name_sz + prefix_sz + 1;
	}
	return _pbcS_build(p , temp, name_sz + prefix_sz + 1);
}

static void
_register_enum(struct pbc_env *p, struct _stringpool *pool, struct pbc_rmessage * enum_type, const char *prefix, int prefix_sz) {
	int field_count = pbc_rmessage_size(enum_type, "value");
	struct map_kv *table = malloc(field_count * sizeof(struct map_kv));
	int i;
	for (i=0;i<field_count;i++) {
		struct pbc_rmessage * value = pbc_rmessage_message(enum_type, "value", i);
		int enum_name_sz;
		const char *enum_name = pbc_rmessage_string(value , "name" , 0 , &enum_name_sz);
		table[i].pointer = (void *)_pbcS_build(pool, enum_name , enum_name_sz);
		table[i].id = pbc_rmessage_integer(value , "number", 0 , 0);
	}
	int name_sz;
	const char * name = pbc_rmessage_string(enum_type, "name", 0 , &name_sz);
	const char *temp = _concat_name(pool, prefix , prefix_sz , name , name_sz, NULL);

	_pbcP_push_enum(p,temp,table,field_count);
	free(table);
}

static void
_set_default(struct _stringpool *pool, struct _field *f , int ptype, const char *value, int sz) {
	if (value == NULL || sz == 0) {
		if (f->type == PTYPE_STRING || f->type == PTYPE_BYTES) {
			f->default_v->s.str = "";
			f->default_v->s.len = 0;
		} else {
			f->default_v->integer.low = 0;
			f->default_v->integer.hi = 0;
		}
		return;
	}

	switch (f->type) {
	case PTYPE_DOUBLE:
	case PTYPE_FLOAT:
		f->default_v->real = strtod(value,NULL);
		break;
	case PTYPE_STRING:
		f->default_v->s.str = _pbcS_build(pool, value , sz);
		f->default_v->s.len = sz;
		break;
	case PTYPE_ENUM:
		// enum default value will be converted to f->default_v->e in bootstrap.c : set_field_one()
		f->default_v->s.str = value;
		f->default_v->s.len = sz;
		break;
	case PTYPE_BOOL:
		if (strcmp(value,"true") == 0) {
			f->default_v->integer.low = 1;
		} else {
			f->default_v->integer.low = 0;
		}
		f->default_v->integer.hi = 0;
		break;
	case PTYPE_UINT64:
	case PTYPE_INT64:
	case PTYPE_SFIXED64:
	case PTYPE_SINT64: {
		long long v = strtoll(value, NULL, 10);
		f->default_v->integer.low = (long) v;
		f->default_v->integer.hi = (long)(v >> 32);
		break;
		}
	case PTYPE_INT32:
	case PTYPE_FIXED32:
	case PTYPE_SFIXED32:
	case PTYPE_SINT32:
		f->default_v->integer.low = strtol(value, NULL, 10);
		if (f->default_v->integer.low < 0) {
			f->default_v->integer.hi = -1;
		} else {
			f->default_v->integer.hi = 0;
		}
		break;
	case PTYPE_UINT32:
		f->default_v->integer.low = strtoul(value, NULL, 10);
		f->default_v->integer.hi = 0;
		break;
	case PTYPE_BYTES:
	case PTYPE_MESSAGE:
		// bytes and message types have no default value
		f->default_v->m.buffer = 0;
		f->default_v->m.len = 0;
		break;
	default:
		f->default_v->integer.low = 0;
		f->default_v->integer.hi = 0;
		break;
	}
}

static void
_register_field(struct pbc_rmessage * field, struct _field * f, struct _stringpool *pool) {
	f->id = pbc_rmessage_integer(field, "number", 0 , 0);
	f->type = pbc_rmessage_integer(field, "type", 0 , 0);	// enum
	f->label = pbc_rmessage_integer(field, "label", 0, 0) - 1; // LABEL_OPTIONAL = 0
	if (pbc_rmessage_size(field , "options") > 0) {
		struct pbc_rmessage * options = pbc_rmessage_message(field, "options" , 0);
		int packed = pbc_rmessage_integer(options , "packed" , 0 , NULL);
		if (packed) {
			f->label = LABEL_PACKED;
		}
	}
	f->type_name.n = pbc_rmessage_string(field, "type_name", 0 , NULL) +1;	// abandon prefix '.' 
	int vsz;
	const char * default_value = pbc_rmessage_string(field, "default_value", 0 , &vsz);
	_set_default(pool , f , f->type, default_value , vsz);
}

static void
_register_extension(struct pbc_env *p, struct _stringpool *pool , const char * prefix, int prefix_sz, struct pbc_rmessage * msg, pbc_array queue) {
	int extension_count = pbc_rmessage_size(msg , "extension");
	if (extension_count <= 0) 
		return;
	int i;

	const char * last = NULL;

	for (i=0;i<extension_count;i++) {
		struct pbc_rmessage * extension = pbc_rmessage_message(msg, "extension", i);
		int field_name_sz = 0;
		struct _field f;
		const char * field_name = pbc_rmessage_string(extension , "name" , 0, &field_name_sz);
		f.name =  _concat_name(pool, prefix, prefix_sz, field_name, field_name_sz, NULL);

		_register_field(extension, &f , pool);

		const char * extendee = pbc_rmessage_string(extension , "extendee" , 0, NULL);

		_pbcP_push_message(p, extendee + 1 , &f , queue);

		if (last == NULL) {
			last = extendee;
		} else if (strcmp(extendee,last) != 0) {
			_pbcP_init_message(p, last+1);
			last = extendee;
		} 
	}
	_pbcP_init_message(p, last+1);
}

static void
_register_message(struct pbc_env *p, struct _stringpool *pool, struct pbc_rmessage * message_type, const char *prefix, int prefix_sz, pbc_array queue) {
	int name_sz;
	const char * name = pbc_rmessage_string(message_type, "name", 0 , &name_sz);
	int sz = 0;
	const char *temp = _concat_name(pool, prefix , prefix_sz , name , name_sz, &sz);

	int field_count = pbc_rmessage_size(message_type, "field");
	int i;
	for (i=0;i<field_count;i++) {
		struct pbc_rmessage * field = pbc_rmessage_message(message_type, "field" , i);
		struct _field f;
		int field_name_sz;
		const char * field_name = pbc_rmessage_string(field, "name", 0 , &field_name_sz);
		f.name = _pbcS_build(pool,field_name,field_name_sz);

		_register_field(field, &f , pool);

		_pbcP_push_message(p, temp , &f , queue);
	}

	_pbcP_init_message(p, temp);

	_register_extension(p, pool, temp, sz,message_type, queue);

	// nested enum

	int enum_count = pbc_rmessage_size(message_type, "enum_type");

	for (i=0;i<enum_count;i++) {
		struct pbc_rmessage * enum_type = pbc_rmessage_message(message_type, "enum_type", i);
		_register_enum(p, pool, enum_type, temp, sz);
	}
	
	// nested type
	int message_count = pbc_rmessage_size(message_type, "nested_type");
	for (i=0;i<message_count;i++) {
		struct pbc_rmessage * nested_type = pbc_rmessage_message(message_type, "nested_type", i);
		_register_message(p, pool, nested_type, temp, sz, queue);
	}
}

static void
_register(struct pbc_env *p, struct pbc_rmessage * file, struct _stringpool *pool) {
	int package_sz;
	const char *package = pbc_rmessage_string(file, "package", 0, &package_sz);

	pbc_array queue;
	_pbcA_open(queue);

	int enum_count = pbc_rmessage_size(file, "enum_type");
	int i;

	for (i=0;i<enum_count;i++) {
		struct pbc_rmessage * enum_type = pbc_rmessage_message(file, "enum_type", i);
		_register_enum(p,  pool , enum_type, package, package_sz);
	}

	int message_count = pbc_rmessage_size(file, "message_type");
	for (i=0;i<message_count;i++) {
		struct pbc_rmessage * message_type = pbc_rmessage_message(file, "message_type", i);
		_register_message(p, pool, message_type, package, package_sz, queue);
	}

	_register_extension(p, pool, package, package_sz, file , queue);

	_pbcB_register_fields(p, queue);

	_pbcA_close(queue);
}

#define CHECK_FILE_OK 0
#define CHECK_FILE_EXIST 1
#define CHECK_FILE_DEPENDENCY 2

static int
_check_file_name(struct pbc_env * p , struct pbc_rmessage * file, const char ** fname) {
	const char * filename = pbc_rmessage_string(file, "name", 0, NULL);
//	printf("reg :%s\n",filename);
	if (_pbcM_sp_query(p->files, filename)) {
		return CHECK_FILE_EXIST;
	}
	int sz = pbc_rmessage_size(file, "dependency"); 
	int i;
	for (i=0;i<sz;i++) {
		const char *dname = pbc_rmessage_string(file,"dependency",i,NULL);
//		printf("dependency :%s\n",dname);
		if (_pbcM_sp_query(p->files, dname) == NULL) {
			return CHECK_FILE_DEPENDENCY;
		}
	}

	*fname = filename;

	return CHECK_FILE_OK;
}

static int
_register_no_dependency(struct pbc_env * p,struct pbc_rmessage ** files , int n ) {
	int r = 0;
	int i;
	for (i=0;i<n;i++) {
		if (files[i] == NULL)
			continue;
		const char *filename = NULL;
		int err = _check_file_name(p, files[i], &filename);
		switch(err) {
		case CHECK_FILE_EXIST:
			break;
		case CHECK_FILE_DEPENDENCY:
			++r;
			break;
		case CHECK_FILE_OK: {
			struct _stringpool *pool = _pbcS_new();
			filename = _pbcS_build(pool, filename , strlen(filename));
			_pbcM_sp_insert(p->files , filename, pool);
			_register(p,files[i],pool);
			files[i] = NULL;
			}
			break;
		}
	}
	return r;
}

int
pbc_register(struct pbc_env * p, struct pbc_slice *slice) {
	struct pbc_rmessage * message = pbc_rmessage_new(p, "google.protobuf.FileDescriptorSet", slice);
	if (message == NULL) {
		p->lasterror = "register open google.protobuf.FileDescriptorSet fail";
		return 1;
	}
	int n = pbc_rmessage_size(message, "file");
	struct pbc_rmessage * files[n];
	int i;
	if (n == 0) {
		p->lasterror = "register empty";
		goto _error;
	}
	for (i=0;i<n;i++) {
		files[i] = pbc_rmessage_message(message, "file", i);
		if (files[i] == NULL) {
			p->lasterror = "register open fail";
			goto _error;
		}
	}

	int r = n;
	do {
		int rr = _register_no_dependency(p,files , n);
		if (rr == r) {
			p->lasterror = "register dependency error";
			goto _error;
		}
		r = rr;
	} while (r>0);

	pbc_rmessage_delete(message);
	return 0;
_error:
	pbc_rmessage_delete(message);
	return 1;
}
