#include "pbc.h"
#include "alloc.h"
#include "map.h"
#include "context.h"
#include "proto.h"
#include "pattern.h"
#include "varint.h"

#include <stddef.h>
#include <string.h>

struct pbc_rmessage {
		struct _message * msg;
		struct map_sp * index;	// key -> struct value *
		struct heap * heap;
};

union _var {
	pbc_var var;
	pbc_array array;
	struct pbc_rmessage message;
} ;

struct value {
	struct _field * type;
	union _var v;
};

int 
pbc_rmessage_next(struct pbc_rmessage *m, const char **key) {
	struct value * v = _pbcM_sp_next(m->index, key);
	if (*key == NULL) {
		return 0;
	}
	return _pbcP_type(v->type, NULL);
}

#define SIZE_VAR (offsetof(struct value, v) + sizeof(pbc_var))
#define SIZE_ARRAY (offsetof(struct value, v) + sizeof(pbc_array))
#define SIZE_MESSAGE (offsetof(struct value, v) + sizeof(struct pbc_rmessage))

static struct value *
read_string(struct heap *h, struct atom *a,struct _field *f, uint8_t *buffer) {
	const char * temp = (const char *) (buffer + a->v.s.start);
	int len = a->v.s.end - a->v.s.start;

	if (len > 0 && temp[len-1] == '\0') {
		struct value * v = _pbcH_alloc(h, SIZE_VAR);
		v->v.var->s.str = temp;
		v->v.var->s.len = len;
		return v;
	} else {
		struct value * v = _pbcH_alloc(h, SIZE_VAR + len + 1);
		memcpy(((char *)v) + SIZE_VAR , temp, len);
		*(((char *)v) + SIZE_VAR + len) = '\0';
		v->v.var->s.str = ((char *)v) + SIZE_VAR;
		v->v.var->s.len = len;
		return v;
	}
}

static void
read_string_var(struct heap *h, pbc_var var,struct atom *a,struct _field *f,uint8_t *buffer) {
	const char * temp = (const char *) (buffer + a->v.s.start);
	int len = a->v.s.end - a->v.s.start;
	if (len == 0) {
		var->s.str = "";
		var->s.len = 0;
	}
	else if (temp[len-1] == '\0') {
		var->s.str = temp;
		var->s.len = len;
	} else {
		char * temp2 = _pbcH_alloc(h, len + 1);
		memcpy(temp2, temp, len);
		temp2[len]='\0';
		var->s.str = temp2;
		var->s.len = -len;
	}
}

static void _pbc_rmessage_new(struct pbc_rmessage * ret , struct _message * type ,  void *buffer, int size, struct heap *h);

static struct value *
read_value(struct heap *h, struct _field *f, struct atom * a, uint8_t *buffer) {
	struct value * v;

	switch (f->type) {
	case PTYPE_DOUBLE:
		CHECK_BIT64(a,NULL);
		v = _pbcH_alloc(h, SIZE_VAR);
		v->v.var->real = read_double(a);
		break;
	case PTYPE_FLOAT:
		CHECK_BIT32(a,NULL);
		v = _pbcH_alloc(h, SIZE_VAR);
		v->v.var->real = (double) read_float(a);
		break;
	case PTYPE_ENUM:
		CHECK_VARINT(a,NULL);
		v = _pbcH_alloc(h, SIZE_VAR);
		v->v.var->e.id = a->v.i.low;
		v->v.var->e.name = _pbcM_ip_query(f->type_name.e->id , a->v.i.low);
		break;
	case PTYPE_INT64:
	case PTYPE_UINT64:
	case PTYPE_INT32:
	case PTYPE_UINT32:
	case PTYPE_BOOL:
		CHECK_VARINT(a,NULL);
		v = _pbcH_alloc(h, SIZE_VAR);
		v->v.var->integer = a->v.i;
		break;
	case PTYPE_FIXED32:
	case PTYPE_SFIXED32:
		CHECK_BIT32(a,NULL);
		v = _pbcH_alloc(h, SIZE_VAR);
		v->v.var->integer = a->v.i;
		break;
	case PTYPE_FIXED64:
	case PTYPE_SFIXED64:
		CHECK_BIT64(a,NULL);
		v = _pbcH_alloc(h, SIZE_VAR);
		v->v.var->integer = a->v.i;
		break;
	case PTYPE_SINT32: 
		CHECK_VARINT(a,NULL);
		v = _pbcH_alloc(h, SIZE_VAR);
		v->v.var->integer = a->v.i;
		_pbcV_dezigzag32(&(v->v.var->integer));
		break;
	case PTYPE_SINT64:
		CHECK_VARINT(a,NULL);
		v = _pbcH_alloc(h, SIZE_VAR);
		v->v.var->integer = a->v.i;
		_pbcV_dezigzag64(&(v->v.var->integer));
		break;
	case PTYPE_STRING:
		CHECK_LEND(a,NULL);
		v = read_string(h,a,f,buffer);
		break;
	case PTYPE_BYTES:
		CHECK_LEND(a,NULL);
		v = _pbcH_alloc(h, SIZE_VAR);
		v->v.var->s.str = (const char *)(buffer + a->v.s.start);
		v->v.var->s.len = a->v.s.end - a->v.s.start;
		break;
	case PTYPE_MESSAGE:
		CHECK_LEND(a,NULL);
		v = _pbcH_alloc(h, SIZE_MESSAGE);
		_pbc_rmessage_new(&(v->v.message), f->type_name.m , 
			buffer + a->v.s.start , 
			a->v.s.end - a->v.s.start,h);
		break;
	default:
		return NULL;
	}
	v->type = f;
	return v;
}

static void
push_value_packed(struct _message * type, pbc_array array, struct _field *f, struct atom * aa, uint8_t *buffer) {
	int n = _pbcP_unpack_packed((uint8_t *)buffer + aa->v.s.start, aa->v.s.end - aa->v.s.start,
		f->type , array);
	if (n<=0) {
		// todo  : error
		type->env->lasterror = "Unpack packed field error";
		return;
	}
	if (f->type == PTYPE_ENUM) {
		int i;
		for (i=0;i<n;i++) {
			union _pbc_var * v = _pbcA_index_p(array, i);
			int id = v->integer.low;
			v->e.id = id;
			v->e.name = _pbcM_ip_query(f->type_name.e->id , id);
		}
	}
}

static void
push_value_array(struct heap *h, pbc_array array, struct _field *f, struct atom * a, uint8_t *buffer) {
	pbc_var v;

	switch (f->type) {
	case PTYPE_DOUBLE:
		v->real = read_double(a);
		break;
	case PTYPE_FLOAT:
		v->real = (double) read_float(a);
		break;
	case PTYPE_ENUM:
		v->e.id = a->v.i.low;
		v->e.name = _pbcM_ip_query(f->type_name.e->id , a->v.i.low);
		break;
	case PTYPE_INT64:
	case PTYPE_UINT64:
	case PTYPE_INT32:
	case PTYPE_UINT32:
	case PTYPE_FIXED32:
	case PTYPE_FIXED64:
	case PTYPE_SFIXED32:
	case PTYPE_SFIXED64:
	case PTYPE_BOOL:
		v->integer = a->v.i;
		break;
	case PTYPE_SINT32: 
		v->integer = a->v.i;
		_pbcV_dezigzag32(&(v->integer));
		break;
	case PTYPE_SINT64:
		v->integer = a->v.i;
		_pbcV_dezigzag64(&(v->integer));
		break;
	case PTYPE_STRING:
		CHECK_LEND(a, );
		read_string_var(h,v,a,f,buffer);
		break;
	case PTYPE_BYTES:
		CHECK_LEND(a, );
		v->s.str = (const char *)(buffer + a->v.s.start);
		v->s.len = a->v.s.end - a->v.s.start;
		break;
	case PTYPE_MESSAGE: {
		CHECK_LEND(a, );
		struct pbc_rmessage message;
		_pbc_rmessage_new(&message, f->type_name.m , 
			buffer + a->v.s.start , 
			a->v.s.end - a->v.s.start,h);
		if (message.msg == NULL) {
			return;
		}
		v->p[0] = message.msg;
		v->p[1] = message.index;
		break;
	}
	default:
		return;
	}

	_pbcA_push(array,v);
}

static void
_pbc_rmessage_new(struct pbc_rmessage * ret , struct _message * type , void *buffer, int size , struct heap *h) {
	if (size == 0) {
		ret->msg = type;
		ret->index = _pbcM_sp_new(0 , h);
		ret->heap = h;
		return;
	}
	pbc_ctx _ctx;
	int count = _pbcC_open(_ctx,buffer,size);
	if (count <= 0) {
		type->env->lasterror = "rmessage decode context error";
		memset(ret , 0, sizeof(*ret));
		return;
	}
	struct context * ctx = (struct context *)_ctx;

	ret->msg = type;
	ret->index = _pbcM_sp_new(count, h);
	ret->heap = h;

	int i;

	for (i=0;i<ctx->number;i++) {
		int id = ctx->a[i].wire_id >> 3;
		struct _field * f = _pbcM_ip_query(type->id , id);
		if (f) {
			if (f->label == LABEL_REPEATED || f->label == LABEL_PACKED) {
				struct value * v;
				void ** vv = _pbcM_sp_query_insert(ret->index, f->name);
				if (*vv == NULL) {
					v = _pbcH_alloc(h, SIZE_ARRAY);
					v->type = f;
					_pbcA_open_heap(v->v.array,ret->heap);
					*vv = v;
				} else {
					v= *vv;
				}
				if (f->label == LABEL_PACKED) {
					push_value_packed(type, v->v.array , f , &(ctx->a[i]), buffer);
					if (pbc_array_size(v->v.array) == 0) {
						type->env->lasterror = "rmessage decode packed data error";
						*vv = NULL;
					}
				} else {
					push_value_array(h,v->v.array , f, &(ctx->a[i]), buffer);
					if (pbc_array_size(v->v.array) == 0) {
						type->env->lasterror = "rmessage decode repeated data error";
						*vv = NULL;
					}
				}
			} else {
				struct value * v = read_value(h, f, &(ctx->a[i]), buffer);
				if (v) {
					_pbcM_sp_insert(ret->index, f->name, v);
				} else {
					type->env->lasterror = "rmessage decode data error";
				}
			}
		}
	}

	_pbcC_close(_ctx);
}

struct pbc_rmessage * 
pbc_rmessage_new(struct pbc_env * env, const char * typename ,  struct pbc_slice * slice) {
	struct _message * msg = _pbcP_get_message(env, typename);
	if (msg == NULL) {
		env->lasterror = "Proto not found";
		return NULL;
	}
	struct pbc_rmessage temp;
	struct heap * h = _pbcH_new(slice->len);
	_pbc_rmessage_new(&temp, msg , slice->buffer, slice->len , h);
	if (temp.msg == NULL) {
		_pbcH_delete(h);
		return NULL;
	}

	struct pbc_rmessage *m = _pbcH_alloc(temp.heap, sizeof(*m));
	*m = temp;
	return m;
}

void 
pbc_rmessage_delete(struct pbc_rmessage * m) {
	if (m) {
		_pbcH_delete(m->heap);
	}
}

const char * 
pbc_rmessage_string(struct pbc_rmessage * m , const char *key , int index, int *sz) {
	struct value * v = _pbcM_sp_query(m->index,key);
	int type = 0;
	pbc_var var;
	if (v == NULL) {
		type = _pbcP_message_default(m->msg, key, var);
	} else {
		if (v->type->label == LABEL_REPEATED || v->type->label == LABEL_PACKED) {
			_pbcA_index(v->v.array, index, var);
		} else {
			var[0] = v->v.var[0];
		}
		type = v->type->type;
	}

	if (type == PTYPE_ENUM) {
		if (sz) {
			*sz = strlen(var->e.name);
		}
		return var->e.name;
	}

	if (sz) {
		int len = var->s.len;
		if (len<0) {
			len = - len;
		}
		*sz = len;
	}
	return var->s.str;
}

uint32_t 
pbc_rmessage_integer(struct pbc_rmessage *m , const char *key , int index, uint32_t *hi) {
	struct value * v = _pbcM_sp_query(m->index,key);
	pbc_var var;
	int type = 0;
	if (v == NULL) {
		type = _pbcP_message_default(m->msg, key, var);
	} else {
		if (v->type->label == LABEL_REPEATED || v->type->label == LABEL_PACKED) {
			_pbcA_index(v->v.array, index, var);
		} else {
			var[0] = v->v.var[0];
		}
		type = v->type->type;
	}

	if (type == PTYPE_ENUM) {
		if (hi) {
			*hi = 0;
		}
		return var->e.id;
	}

	if (hi) {
		*hi = var->integer.hi;
	}
	return var->integer.low;
}

double 
pbc_rmessage_real(struct pbc_rmessage * m, const char *key , int index) {
	struct value * v = _pbcM_sp_query(m->index,key);
	pbc_var var;
	if (v == NULL) {
		_pbcP_message_default(m->msg, key, var);
	} else {
		if (v->type->label == LABEL_REPEATED || v->type->label == LABEL_PACKED) {
			_pbcA_index(v->v.array, index, var);
		} else {
			return v->v.var->real;
		}
	}
	return var->real;
}


struct pbc_rmessage * 
pbc_rmessage_message(struct pbc_rmessage * rm, const char *key, int index) {
	struct value * v = _pbcM_sp_query(rm->index,key);
	if (v == NULL) {
		struct _field * f = _pbcM_sp_query(rm->msg->name, key);
		if (f == NULL) {
			rm->msg->env->lasterror = "Invalid key for sub-message";
			// invalid key
			return NULL;
		}
		struct _message * m = f->type_name.m;

		if (m->def == NULL) {
			// m->def will be free at the end (pbc_delete).
			m->def = malloc(sizeof(struct pbc_rmessage));
			m->def->msg = m;
			m->def->index = NULL;
		}
		return m->def;
	} else {
		if (v->type->label == LABEL_REPEATED) {
			return _pbcA_index_p(v->v.array,index);
		} else {
			return &(v->v.message);
		}
	}
}

int 
pbc_rmessage_size(struct pbc_rmessage *m, const char *key) {
	struct value * v = _pbcM_sp_query(m->index,key);
	if (v == NULL) {
		return 0;
	}
	if (v->type->label == LABEL_REPEATED || v->type->label == LABEL_PACKED) {
		return pbc_array_size(v->v.array);
	} else {
		return 1;
	}
}