#include "pbc.h"
#include "alloc.h"
#include "context.h"
#include "proto.h"
#include "varint.h"

#include <assert.h>

static const char * TYPENAME[] = {
	"invalid",	// 0
	"integer",	// 1
	"real",	// 2
	"boolean",	// 3
	"enum",	// 4
	"string",	// 5
	"message",	// 6
	"fixed64",	// 7
	"fixed32",	// 8
	"bytes",	// 9
	"int64",	// 10
	"uint",	// 11
};

static int
call_unknown(pbc_decoder f, void * ud, int id, struct atom *a, uint8_t * start) {
	union pbc_value v;
	switch (a->wire_id) {
	case WT_VARINT:
		v.i.low = a->v.i.low;
		v.i.hi = a->v.i.hi;
		f(ud, PBC_INT, TYPENAME[PBC_INT], &v, id , NULL);
		break;
	case WT_BIT64:
		v.i.low = a->v.i.low;
		v.i.hi = a->v.i.hi;
		f(ud, PBC_FIXED64, TYPENAME[PBC_FIXED64], &v, id , NULL);
		break;
	case WT_LEND:
		v.s.buffer = (char*)start + a->v.s.start;
		v.s.len = a->v.s.end - a->v.s.start;
		f(ud, PBC_BYTES, TYPENAME[PBC_BYTES], &v, id , NULL);
		break;
	case WT_BIT32:
		v.i.low = a->v.i.low;
		v.i.hi = 0;
		f(ud, PBC_FIXED32, TYPENAME[PBC_FIXED32], &v, id , NULL);
		break;
	default:
		return 1;
	}
	return 0;
}

static int
call_type(pbc_decoder pd, void * ud, struct _field *f, struct atom *a, uint8_t * start) {
	union pbc_value v;
	const char * typename = NULL;
	int type = _pbcP_type(f, &typename);
	assert(type != 0);
	if (typename == NULL) {
		typename = TYPENAME[type & ~PBC_REPEATED];
	}
	switch (f->type) {
	case PTYPE_DOUBLE:
		CHECK_BIT64(a, -1);
		v.f = read_double(a);
		break;
	case PTYPE_FLOAT:
		CHECK_BIT32(a, -1);
		v.f = (double) read_float(a);
		break;
	case PTYPE_ENUM:
		CHECK_VARINT(a, -1);
		v.e.id = a->v.i.low;
		v.e.name = _pbcM_ip_query(f->type_name.e->id , v.e.id);
		break;
	case PTYPE_INT64:
	case PTYPE_UINT64:
		CHECK_VARINT(a, -1);
		v.i.low = a->v.i.low;
		v.i.hi = a->v.i.hi;
		break;
	case PTYPE_FIXED64:
	case PTYPE_SFIXED64:
		CHECK_BIT64(a, -1);
		v.i.low = a->v.i.low;
		v.i.hi = a->v.i.hi;
		break;
	case PTYPE_INT32:
	case PTYPE_UINT32:
	case PTYPE_BOOL:
		CHECK_VARINT(a, -1);
		v.i.low = a->v.i.low;
		v.i.hi = 0;
		break;
	case PTYPE_FIXED32:
	case PTYPE_SFIXED32:
		CHECK_BIT32(a, -1);
		v.i.low = a->v.i.low;
		v.i.hi = 0;
		break;
	case PTYPE_SINT32: 
		CHECK_VARINT(a, -1);
		v.i.low = a->v.i.low;
		v.i.hi = a->v.i.hi;
		_pbcV_dezigzag32((struct longlong *)&(v.i));
		break;
	case PTYPE_SINT64:
		CHECK_VARINT(a, -1);
		v.i.low = a->v.i.low;
		v.i.hi = a->v.i.hi;
		_pbcV_dezigzag64((struct longlong *)&(v.i));
		break;
	case PTYPE_STRING:
	case PTYPE_BYTES:
	case PTYPE_MESSAGE:
		CHECK_LEND(a, -1);
		v.s.buffer = start + a->v.s.start;
		v.s.len = a->v.s.end - a->v.s.start;
		break;
	default:
		assert(0);
		break;
	}
	pd(ud, type, typename, &v, f->id, f->name);
	return 0;
}

static int
call_array(pbc_decoder pd, void * ud, struct _field *f, uint8_t * buffer , int size) {
	union pbc_value v;
	const char * typename = NULL;
	int type = _pbcP_type(f, &typename);
	assert(type != 0);
	if (typename == NULL) {
		typename = TYPENAME[type & ~PBC_REPEATED];
	}
	v.i.hi = 0;
	int i;
	switch(f->type) {
		case PTYPE_DOUBLE:
			if (size % 8 != 0) {
				return -1;
			}
			for (i=0;i<size;i+=8) {
				union {
					double d;
					uint64_t i64;
				} u;
				u.i64 = (uint64_t)buffer[i] |
					(uint64_t)buffer[i+1] << 8 |
					(uint64_t)buffer[i+2] << 16 |
					(uint64_t)buffer[i+3] << 24 |
					(uint64_t)buffer[i+4] << 32 |
					(uint64_t)buffer[i+5] << 40 |
					(uint64_t)buffer[i+6] << 48 |
					(uint64_t)buffer[i+7] << 56;
				v.f = u.d;
				pd(ud, type , typename, &v, f->id, f->name);
			}
			return size/8;
		case PTYPE_FLOAT:
			if (size % 4 != 0)
				return -1;
			for (i=0;i<size;i+=4) {
				union {
					float f;
					uint32_t i32;
				} u;
				u.i32 = (uint32_t)buffer[i] |
					(uint32_t)buffer[i+1] << 8 |
					(uint32_t)buffer[i+2] << 16 |
					(uint32_t)buffer[i+3] << 24;
				v.f = (double)u.f;
				pd(ud, type , typename, &v, f->id, f->name);
			}
			return size/4;
		case PTYPE_FIXED32:
		case PTYPE_SFIXED32:
			if (size % 4 != 0)
				return -1;
			for (i=0;i<size;i+=4) {
				v.i.low = (uint32_t)buffer[i] |
					(uint32_t)buffer[i+1] << 8 |
					(uint32_t)buffer[i+2] << 16 |
					(uint32_t)buffer[i+3] << 24;
				pd(ud, type , typename, &v, f->id, f->name);
			}
			return size/4;
		case PTYPE_FIXED64:
		case PTYPE_SFIXED64:
			if (size % 8 != 0)
				return -1;
			for (i=0;i<size;i+=8) {
				v.i.low = (uint32_t)buffer[i] |
					(uint32_t)buffer[i+1] << 8 |
					(uint32_t)buffer[i+2] << 16 |
					(uint32_t)buffer[i+3] << 24;
				v.i.hi = (uint32_t)buffer[i+4] |
					(uint32_t)buffer[i+5] << 8 |
					(uint32_t)buffer[i+6] << 16 |
					(uint32_t)buffer[i+7] << 24;
				pd(ud, type , typename, &v, f->id, f->name);
			}
			return size/8;
		case PTYPE_INT64:
		case PTYPE_UINT64:
		case PTYPE_INT32:
		case PTYPE_UINT32:
		case PTYPE_BOOL: {
			int n = 0;
			while (size > 0) {
				int len;
				if (size >= 10) {
					len = _pbcV_decode(buffer, (struct longlong *)&(v.i));
				} else {
					uint8_t temp[10];
					memcpy(temp, buffer, size);
					len = _pbcV_decode(buffer, (struct longlong *)&(v.i));
					if (len > size)
						return -1;
				}
				pd(ud, type , typename, &v, f->id, f->name);
				buffer += len;
				size -= len;
				++n;
			}
			return n;
		}
		case PTYPE_ENUM: {
			int n = 0;
			while (size > 0) {
				int len;
				if (size >= 10) {
					len = _pbcV_decode(buffer, (struct longlong *)&(v.i));
				} else {
					uint8_t temp[10];
					memcpy(temp, buffer, size);
					len = _pbcV_decode(buffer, (struct longlong *)&(v.i));
					if (len > size)
						return -1;
				}
				v.e.id = v.i.low;
				v.e.name = _pbcM_ip_query(f->type_name.e->id , v.i.low);
				pd(ud, type , typename, &v, f->id, f->name);
				buffer += len;
				size -= len;
				++n;
			}
			return n;
		}
		case PTYPE_SINT32: {
			int n = 0;
			while (size > 0) {
				int len;
				if (size >= 10) {
					len = _pbcV_decode(buffer, (struct longlong *)&(v.i));
					_pbcV_dezigzag32((struct longlong *)&(v.i));
				} else {
					uint8_t temp[10];
					memcpy(temp, buffer, size);
					len = _pbcV_decode(buffer, (struct longlong *)&(v.i));
					if (len > size)
						return -1;
					_pbcV_dezigzag32((struct longlong *)&(v.i));
				}
				pd(ud, type , typename, &v, f->id, f->name);
				buffer += len;
				size -= len;
				++n;
			}
			return n;
		}
		case PTYPE_SINT64: {
			int n = 0;
			while (size > 0) {
				int len;
				if (size >= 10) {
					len = _pbcV_decode(buffer, (struct longlong *)&(v.i));
					_pbcV_dezigzag64((struct longlong *)&(v.i));
				} else {
					uint8_t temp[10];
					memcpy(temp, buffer, size);
					len = _pbcV_decode(buffer, (struct longlong *)&(v.i));
					if (len > size)
						return -1;
					_pbcV_dezigzag64((struct longlong *)&(v.i));
				}
				pd(ud, type , typename, &v, f->id, f->name);
				buffer += len;
				size -= len;
				++n;
			}
			return n;
		}
		default:
			return -1;
	}
}

int
pbc_decode(struct pbc_env * env, const char * typename , struct pbc_slice * slice, pbc_decoder pd, void *ud) {
	struct _message * msg = _pbcP_get_message(env, typename);
	if (msg == NULL) {
		env->lasterror = "Proto not found";
		return -1;
	}
	if (slice->len == 0) {
		return 0;
	}
	pbc_ctx _ctx;
	int count = _pbcC_open(_ctx,slice->buffer,slice->len);
	if (count <= 0) {
		env->lasterror = "decode context error";
		_pbcC_close(_ctx);
		return count - 1;
	}
	struct context * ctx = (struct context *)_ctx;
	uint8_t * start = slice->buffer;

	int i;
	for (i=0;i<ctx->number;i++) {
		int id = ctx->a[i].wire_id >> 3;
		struct _field * f = _pbcM_ip_query(msg->id , id);
		if (f==NULL) {
			int err = call_unknown(pd,ud,id,&ctx->a[i],start);
			if (err) {
				_pbcC_close(_ctx);
				return -i-1;
			}
		} else if (f->label == LABEL_PACKED) {
			struct atom * a = &ctx->a[i];
			int n = call_array(pd, ud, f , start + a->v.s.start , a->v.s.end - a->v.s.start);
			if (n < 0) {
				_pbcC_close(_ctx);
				return -i-1;
			}
		} else {
			if (call_type(pd,ud,f,&ctx->a[i],start) != 0) {
				_pbcC_close(_ctx);
				return -i-1;
			}
		}
	}

	_pbcC_close(_ctx);
	return ctx->number;
}

