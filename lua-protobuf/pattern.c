#include "pbc.h"
#include "alloc.h"
#include "context.h"
#include "varint.h"
#include "pattern.h"
#include "array.h"
#include "proto.h"
#include "map.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>

static void
set_default_v(void * output, int ctype, pbc_var defv) {
	switch (ctype) {
	case CTYPE_INT32:
		*(uint32_t *)output = defv->integer.low;
		break;
	case CTYPE_INT64:
		*(uint64_t *)output = (uint64_t)defv->integer.low | (uint64_t)defv->integer.hi << 32;
		break;
	case CTYPE_DOUBLE:
		*(double *)output = defv->real;
		break;
	case CTYPE_FLOAT:
		*(float *)output = (float)defv->real;
		break;
	case CTYPE_BOOL:
		*(bool *)output = (defv->integer.low != 0);
		break;
	case CTYPE_INT8:
		*(uint8_t *)output = (uint8_t)defv->integer.low;
		break;
	case CTYPE_INT16:
		*(uint16_t *)output = (uint16_t)defv->integer.low;
		break;
	case CTYPE_VAR:
		*(union _pbc_var *)output = *defv;
		break;
	}
}

static void
_pattern_set_default(struct _pattern_field *field, char *output) {
	if (field->ctype == CTYPE_ARRAY || field->ctype == CTYPE_PACKED) {
		struct _pbc_array *array = (struct _pbc_array *)(output + field->offset);
		_pbcA_open(array);
	} else if (field->ptype == PTYPE_ENUM) {
		pbc_var defv;
		defv->integer.low = field->defv->e.id;
		defv->integer.hi = 0;
		set_default_v(output + field->offset, field->ctype, defv);
	}
	set_default_v(output + field->offset, field->ctype, field->defv);
}

void
pbc_pattern_set_default(struct pbc_pattern *pat, void *output) {
	int i;
	for (i=0;i<pat->count;i++) {
		_pattern_set_default(&pat->f[i], output);
	}
}

// pattern unpack

static struct _pattern_field *
bsearch_pattern(struct pbc_pattern *pat, int id)
{
	int begin = 0;
	int end = pat->count;
	while (begin < end) {
		int mid = (begin + end)/2;
		struct _pattern_field * f = &pat->f[mid];
		if (id == f->id) {
			return f;
		}
		if (id < f->id) {
			end = mid;
		} else {
			begin = mid + 1;
		}
	}
	return NULL;
}

static inline int
write_real(int ctype, double v, void *out) {
	switch(ctype) {
	case CTYPE_DOUBLE:
		*(double *)out = v;
		return 0;
	case CTYPE_FLOAT:
		*(float *)out = (float)v;
		return 0;
	case CTYPE_VAR:
		((union _pbc_var *)out)->real = v;
		return 0;
	}
	return -1;
}

static inline int
write_longlong(int ctype, struct longlong *i, void *out) {
	switch(ctype) {
	case CTYPE_INT32:
		*(uint32_t *)out = i->low;
		return 0;
	case CTYPE_INT64:
		*(uint64_t *)out = (uint64_t)i->low | (uint64_t)i->hi << 32;
		return 0;
	case CTYPE_BOOL:
		*(bool *)out = (i->low !=0) ;
		return 0;
	case CTYPE_INT8:
		*(uint8_t *)out = (uint8_t)i->low;
		return 0;
	case CTYPE_INT16:
		*(uint8_t *)out = (uint16_t)i->low;
		return 0;
	case CTYPE_VAR:
		((union _pbc_var *)out)->integer = *i;
		return 0;
	}
	return -1;
}

static inline int
write_integer(int ctype, struct atom *a, void *out) {
	return write_longlong(ctype, &(a->v.i), out);
}

static int unpack_array(int ptype, char *buffer, struct atom *, pbc_array _array);

int
_pbcP_unpack_packed(uint8_t *buffer, int size, int ptype, pbc_array array) {
	pbc_var var;
	var->integer.hi = 0;
	int i;
	switch(ptype) {
	case PTYPE_DOUBLE:
		if (size % 8 != 0)
			return -1;
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
			var->real = u.d;
			_pbcA_push(array, var);
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
			var->real = (double)u.f;
			_pbcA_push(array, var);
		}
		return size/4;
	case PTYPE_FIXED32:
	case PTYPE_SFIXED32:
		if (size % 4 != 0)
			return -1;
		for (i=0;i<size;i+=4) {
			var->integer.low = (uint32_t)buffer[i] |
				(uint32_t)buffer[i+1] << 8 |
				(uint32_t)buffer[i+2] << 16 |
				(uint32_t)buffer[i+3] << 24;
			_pbcA_push(array, var);
		}
		return size/4;
	case PTYPE_FIXED64:
	case PTYPE_SFIXED64:
		if (size % 8 != 0)
			return -1;
		for (i=0;i<size;i+=8) {
			var->integer.low = (uint32_t)buffer[i] |
				(uint32_t)buffer[i+1] << 8 |
				(uint32_t)buffer[i+2] << 16 |
				(uint32_t)buffer[i+3] << 24;
			var->integer.hi = (uint32_t)buffer[i+4] |
				(uint32_t)buffer[i+5] << 8 |
				(uint32_t)buffer[i+6] << 16 |
				(uint32_t)buffer[i+7] << 24;
			_pbcA_push(array, var);
		}
		return size/8;
	case PTYPE_INT64:
	case PTYPE_UINT64:
	case PTYPE_INT32:
	case PTYPE_UINT32:
	case PTYPE_ENUM:	// enum must be integer type in pattern mode
	case PTYPE_BOOL: {
		int n = 0;
		while (size > 0) {
			int len;
			if (size >= 10) {
				len = _pbcV_decode(buffer, &(var->integer));
			} else {
				uint8_t temp[10];
				memcpy(temp, buffer, size);
				len = _pbcV_decode(buffer, &(var->integer));
				if (len > size)
					return -1;
			}
			_pbcA_push(array, var);
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
				len = _pbcV_decode(buffer, &(var->integer));
				_pbcV_dezigzag32(&(var->integer));
			} else {
				uint8_t temp[10];
				memcpy(temp, buffer, size);
				len = _pbcV_decode(buffer, &(var->integer));
				if (len > size)
					return -1;
				_pbcV_dezigzag32(&(var->integer));
			}
			_pbcA_push(array, var);
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
				len = _pbcV_decode(buffer, &(var->integer));
				_pbcV_dezigzag64(&(var->integer));
			} else {
				uint8_t temp[10];
				memcpy(temp, buffer, size);
				len = _pbcV_decode(buffer, &(var->integer));
				if (len > size)
					return -1;
				_pbcV_dezigzag64(&(var->integer));
			}
			_pbcA_push(array, var);
			buffer += len;
			size -= len;
			++n;
		}
		return n;
	}
	}
	return -1;
}

static int
unpack_field(int ctype, int ptype, char * buffer, struct atom * a, void *out) {
	if (ctype == CTYPE_ARRAY) {
		return unpack_array(ptype, buffer, a , out);
	}
	if (ctype == CTYPE_PACKED) {
		return _pbcP_unpack_packed((uint8_t *)buffer + a->v.s.start, a->v.s.end - a->v.s.start,	ptype, out);
	}
	switch(ptype) {
	case PTYPE_DOUBLE:
		CHECK_BIT64(a, -1);
		return write_real(ctype, read_double(a), out);
	case PTYPE_FLOAT:
		CHECK_BIT32(a, -1);
		return write_real(ctype, read_float(a), out);
	case PTYPE_INT64:
	case PTYPE_UINT64:
	case PTYPE_INT32:
	case PTYPE_UINT32:
	case PTYPE_ENUM:	// enum must be integer type in pattern mode
	case PTYPE_BOOL:
		CHECK_VARINT(a, -1);
		return write_integer(ctype, a , out);
	case PTYPE_FIXED32:
	case PTYPE_SFIXED32:
		CHECK_BIT32(a, -1);
		return write_integer(ctype, a , out);
	case PTYPE_FIXED64:
	case PTYPE_SFIXED64:
		CHECK_BIT64(a, -1);
		return write_integer(ctype, a , out);
	case PTYPE_SINT32: {
		CHECK_VARINT(a, -1);
		struct longlong temp = a->v.i;
		_pbcV_dezigzag32(&temp);
		return write_longlong(ctype, &temp , out);
	}
	case PTYPE_SINT64: {
		CHECK_LEND(a, -1);
		struct longlong temp = a->v.i;
		_pbcV_dezigzag64(&temp);
		return write_longlong(ctype, &temp , out);
	}
	case PTYPE_MESSAGE: 
		CHECK_LEND(a, -1);
		((union _pbc_var *)out)->m.buffer = buffer + a->v.s.start;
		((union _pbc_var *)out)->m.len = a->v.s.end - a->v.s.start;
		return 0;
	case PTYPE_STRING:
	case PTYPE_BYTES:
		CHECK_LEND(a, -1);
		((struct pbc_slice *)out)->buffer = buffer + a->v.s.start;
		((struct pbc_slice *)out)->len = a->v.s.end - a->v.s.start;
		return 0;
	}
	return -1;
}

static int 
unpack_array(int ptype, char *buffer, struct atom * a, pbc_array _array) {
	pbc_var var;
	int r = unpack_field(CTYPE_VAR, ptype, buffer, a , var);
	if (r !=0 )
		return r;
	_pbcA_push(_array , var);

	return 0;
}

void 
pbc_pattern_close_arrays(struct pbc_pattern *pat, void * data) {
	int i;
	for (i=0;i<pat->count;i++) {
		if (pat->f[i].ctype == CTYPE_ARRAY || pat->f[i].ctype == CTYPE_PACKED) {
			void *array = (char *)data + pat->f[i].offset;
			_pbcA_close(array);
		}
	}
}

static inline int
_pack_wiretype(uint32_t wt, struct pbc_slice *s) {
	int len;
	if (s->len < 10) {
		uint8_t temp[10];
		len = _pbcV_encode32(wt, temp);
		if (len > s->len)
			return -1;
		memcpy(s->buffer, temp, len);
	} else {
		len = _pbcV_encode32(wt, s->buffer);
	}
	s->buffer = (char *)s->buffer + len;
	s->len -= len;
	return len;
}

static inline int
_pack_varint64(uint64_t i64, struct pbc_slice *s) {
	int len;
	if (s->len < 10) {
		uint8_t temp[10];
		len = _pbcV_encode(i64, temp);
		if (len > s->len)
			return -1;
		memcpy(s->buffer, temp, len);
	} else {
		len = _pbcV_encode(i64, s->buffer);
	}
	s->buffer = (char *)s->buffer + len;
	s->len -= len;
	return len;
}

static inline int
_pack_sint32(uint32_t v, struct pbc_slice *s) {
	int len;
	if (s->len < 10) {
		uint8_t temp[10];
		len = _pbcV_zigzag32(v, temp);
		if (len > s->len)
			return -1;
		memcpy(s->buffer, temp, len);
	} else {
		len = _pbcV_zigzag32(v, s->buffer);
	}
	s->buffer = (char *)s->buffer + len;
	s->len -= len;
	return len;
}

static inline int
_pack_sint64(uint64_t v, struct pbc_slice *s) {
	int len;
	if (s->len < 10) {
		uint8_t temp[10];
		len = _pbcV_zigzag(v, temp);
		if (len > s->len)
			return -1;
		memcpy(s->buffer, temp, len);
	} else {
		len = _pbcV_zigzag(v, s->buffer);
	}
	s->buffer = (char *)s->buffer + len;
	s->len -= len;
	return len;
}

static inline void
_fix32_encode(uint32_t v , uint8_t *buffer) {
	buffer[0] = (uint8_t) v;
	buffer[1] = (uint8_t) (v >> 8);
	buffer[2] = (uint8_t) (v >> 16);
	buffer[3] = (uint8_t) (v >> 24);
}

static inline void
_fix64_encode(struct longlong *v , uint8_t *buffer) {
	_fix32_encode(v->low , buffer);
	_fix32_encode(v->hi, buffer + 4);
}

static int
_pack_number(int ptype , int ctype , struct pbc_slice *s, void *input) {
	pbc_var var;
	if (ctype == CTYPE_VAR) {
		memcpy(var, input, sizeof(var));
	} else {
		switch (ctype) {
		case CTYPE_INT32:
			var->integer.low = *(uint32_t *)input;
			var->integer.hi = 0;
			break;
		case CTYPE_INT64: {
			uint64_t v = *(uint64_t *)input;
			var->integer.low = (uint32_t) (v & 0xffffffff);
			var->integer.hi = (uint32_t) (v >> 32);
			break;
		}
		case CTYPE_INT16:
			var->integer.low = *(uint16_t *)input;
			var->integer.hi = 0;
			break;
		case CTYPE_INT8:
			var->integer.low = *(uint8_t *)input;
			var->integer.hi = 0;
			break;
		case CTYPE_BOOL:
			var->integer.low = *(bool *)input;
			var->integer.hi = 0;
			break;
		case CTYPE_DOUBLE:
			var->real = *(double *)input;
			break;
		case CTYPE_FLOAT:
			var->real = *(float *)input;
			break;
		}
	}

	switch(ptype) {
	case PTYPE_FIXED64:
	case PTYPE_SFIXED64:
		if (s->len < 8)
			return -1;
		_fix64_encode(&(var->integer), s->buffer);
		s->buffer = (char *)s->buffer + 8;
		s->len -= 8;
		return 8;
	case PTYPE_DOUBLE:
		if (s->len < 8)
			return -1;
		double_encode(var->real , s->buffer);
		s->buffer = (char *)s->buffer + 8;
		s->len -= 8;
		return 8;
	case PTYPE_FLOAT:
		if (s->len < 4)
			return -1;
		float_encode((float)var->real , s->buffer);
		s->buffer = (char *)s->buffer + 4;
		s->len -= 4;
		return 4;
	case PTYPE_FIXED32:
	case PTYPE_SFIXED32:
		if (s->len < 4)
			return -1;
		_fix32_encode(var->integer.low, s->buffer);
		s->buffer = (char *)s->buffer + 4;
		s->len -= 4;
		return 4;
	case PTYPE_UINT64:
	case PTYPE_INT64:
	case PTYPE_INT32:
		return _pack_varint64((uint64_t)var->integer.low | (uint64_t)var->integer.hi << 32, s);
	case PTYPE_UINT32:
	case PTYPE_BOOL:
	case PTYPE_ENUM:
		return _pack_wiretype(var->integer.low , s);
	case PTYPE_SINT32: 
		return _pack_sint32(var->integer.low , s);
	case PTYPE_SINT64:
		return _pack_sint64((uint64_t)var->integer.low | (uint64_t)var->integer.hi << 32 , s);
	default:
		return -1;
	}
}

static int
_pack_field(struct _pattern_field *pf , int ctype, struct pbc_slice *s, void *input) {
	int wiretype;
	int ret = 0;
	int len;
	struct pbc_slice * input_slice;
	struct pbc_slice string_slice;

	switch(pf->ptype) {
	case PTYPE_FIXED64:
	case PTYPE_SFIXED64:
	case PTYPE_DOUBLE:
		wiretype = WT_BIT64;
		goto _number;
	case PTYPE_FIXED32:
	case PTYPE_SFIXED32:
	case PTYPE_FLOAT:
		wiretype = WT_BIT32;
		goto _number;
	case PTYPE_UINT64:
	case PTYPE_INT64:
	case PTYPE_INT32:
	case PTYPE_BOOL:
	case PTYPE_UINT32:
	case PTYPE_ENUM:
	case PTYPE_SINT32:
	case PTYPE_SINT64:
		wiretype = WT_VARINT;
		goto _number;
	case PTYPE_STRING:
		wiretype = WT_LEND;
		input_slice = input;
		if (input_slice->len > 0)
			goto _string;
		string_slice.buffer = input_slice->buffer;
		string_slice.len = strlen((const char *)string_slice.buffer) - input_slice->len;
		input_slice = &string_slice;
	
		goto _string;
	case PTYPE_MESSAGE:
	case PTYPE_BYTES:
		wiretype = WT_LEND;
		goto _bytes;
	default:
		break;
	}

	return 0;
_bytes:
	input_slice = input;
_string:
	len = _pack_wiretype(pf->id << 3 | WT_LEND , s);
	if (len < 0) {
		return len;
	}
	ret += len;
	len = _pack_wiretype(input_slice->len , s);
	if (len < 0) {
		return len;
	}
	ret += len;
	if (input_slice->len > s->len)
		return -1;
	memcpy(s->buffer , input_slice->buffer, input_slice->len);
	ret += input_slice->len;
	s->buffer = (char *)s->buffer + input_slice->len;
	s->len -= input_slice->len;

	return ret;
_number:
	len = _pack_wiretype(pf->id << 3 | wiretype , s);
	if (len < 0) {
		return len;
	}
	ret += len;
	len = _pack_number(pf->ptype, ctype , s, input);
	if (len < 0) {
		return len;
	}
	ret += len;

	return ret;
}

static int 
_pack_repeated(struct _pattern_field *pf , struct pbc_slice *s, pbc_array array) {
	int n = pbc_array_size(array);
	int ret = 0;
	if (n>0) {
		int i;
		for (i=0;i<n;i++) {
			int len = _pack_field(pf , CTYPE_VAR , s , _pbcA_index_p(array , i));
			if (len < 0)
				return len;
			ret += len;
		}
	}
	return ret;
}

static int
_pack_packed_fixed(struct _pattern_field *pf , int width, struct pbc_slice *s, pbc_array array) {
	int len;
	int n = pbc_array_size(array);
	len = _pack_wiretype(n * width , s);
	if (len < 0) {
		return len;
	}
	if (s->len - len <  n * width)
		return -1;
	int i;
	for (i=0;i<n;i++) {
		_pack_number(pf->ptype, CTYPE_VAR , s, _pbcA_index_p(array, i));
	}

	return len + n * width;
}

static int
_pack_packed_varint(struct _pattern_field *pf , struct pbc_slice *slice, pbc_array array) {
	struct pbc_slice s = * slice;
	int n = pbc_array_size(array);
	int estimate = n; 
	int estimate_len = _pack_wiretype(estimate , &s);
	if (estimate_len < 0) {
		return -1;
	}
	int i;
	int packed_len = 0;
	for (i=0;i<n;i++) {
		int len	= _pack_number(pf->ptype, CTYPE_VAR , &s, _pbcA_index_p(array, i));
		if (len < 0)
			return -1;
		packed_len += len;
	}
	if (packed_len == estimate) {
		*slice = s;
		return packed_len + estimate_len;
	}
	uint8_t temp[10];
	struct pbc_slice header_slice = { temp , 10 };
	int header_len = _pack_wiretype(packed_len , &header_slice);
	if (header_len == estimate_len) {
		memcpy(slice->buffer , temp , header_len);
		*slice = s;
		return packed_len + estimate_len;
	}
	if (header_len + packed_len > slice->len)
		return -1;
	memmove((char *)slice->buffer + header_len , (char *)slice->buffer + estimate_len, packed_len);
	memcpy(slice->buffer , temp , header_len);
	slice->buffer = (char *)slice->buffer + packed_len + header_len;
	slice->len -= packed_len + header_len;
	return packed_len + header_len;
}

static int 
_pack_packed(struct _pattern_field *pf , struct pbc_slice *s, pbc_array array) {
	int n = pbc_array_size(array);
	if (n == 0)
		return 0;

	int ret = 0;
	int len;
	len = _pack_wiretype(pf->id << 3 | WT_LEND , s);
	if (len < 0) {
		return len;
	}
	ret += len;

	switch (pf->ptype) {
	case PTYPE_FIXED64:
	case PTYPE_SFIXED64:
	case PTYPE_DOUBLE:
		len = _pack_packed_fixed(pf, 8, s , array);
		if (len < 0)
			return len;
		break;
	case PTYPE_FIXED32:
	case PTYPE_SFIXED32:
	case PTYPE_FLOAT:
		len = _pack_packed_fixed(pf, 4, s , array);
		if (len < 0)
			return len;
		break;
	case PTYPE_UINT64:
	case PTYPE_INT64:
	case PTYPE_INT32:
	case PTYPE_BOOL:
	case PTYPE_UINT32:
	case PTYPE_ENUM:
	case PTYPE_SINT32:
	case PTYPE_SINT64:
		len = _pack_packed_varint(pf, s, array);
		if (len < 0)
			return len;
		break;
	}
	ret += len;

	return ret;
}

static bool
_is_default(struct _pattern_field * pf, void * in) {
	switch (pf->ctype) {
	case CTYPE_INT64: {
		struct longlong * d64 = &pf->defv->integer;
		return ((uint64_t)d64->low | (uint64_t)d64->hi << 32) == *(uint64_t *)in;
	}
	case CTYPE_DOUBLE: 
		return pf->defv->real == *(double *)in;
	case CTYPE_FLOAT:
		return (float)(pf->defv->real) == *(float *)in;
	case CTYPE_INT32:
		return pf->defv->integer.low == *(uint32_t *)in;
	case CTYPE_INT16:
		return (uint16_t)(pf->defv->integer.low) == *(uint16_t *)in;
	case CTYPE_INT8:
		return (uint8_t)(pf->defv->integer.low) == *(uint8_t *)in;
	case CTYPE_BOOL:
		if (pf->defv->integer.low)
			return *(bool *)in == true;
		else
			return *(bool *)in == false;
	}
	if (pf->ptype == PTYPE_STRING) {
		struct pbc_slice *slice = in;
		if (slice->buffer == NULL) {
			return pf->defv->s.str[0] == '\0';
		}
		int len = slice->len;
		if (len <= 0) {
			return strcmp(pf->defv->s.str, slice->buffer) == 0;
		}
		return len == pf->defv->s.len && memcmp(pf->defv->s.str, slice->buffer, len)==0;
	}

	return false;
}

int 
pbc_pattern_pack(struct pbc_pattern *pat, void *input, struct pbc_slice * s)
{
	struct pbc_slice slice = *s;
	int i;
	for (i=0;i<pat->count;i++) {
		struct _pattern_field * pf = &pat->f[i];
		void * in = (char *)input + pf->offset;
		int len = 0;
		switch(pf->label) {
		case LABEL_OPTIONAL:
			if (_is_default(pf , in)) {
				break;
			}
		case LABEL_REQUIRED:
			len = _pack_field(pf, pf->ctype, &slice, in);
			break;
		case LABEL_REPEATED:
			len = _pack_repeated(pf, &slice , in);
			break;
		case LABEL_PACKED:
			len = _pack_packed(pf, &slice , in);
			break;
		}
		if (len < 0) {
			return len;
		}
	}
	int len = (char *)slice.buffer - (char *)s->buffer;
	int ret = s->len - len;
	s->len = len;
	return ret;
}

int 
pbc_pattern_unpack(struct pbc_pattern *pat, struct pbc_slice *s, void * output) {
	if (s->len == 0) {
		pbc_pattern_set_default(pat, output);
		return 0;
	}
	pbc_ctx _ctx;
	int r = _pbcC_open(_ctx, s->buffer, s->len);
	if (r <= 0) {
		pat->env->lasterror = "Pattern unpack open context error";
		_pbcC_close(_ctx);
		return r-1;
	}

	struct context * ctx = (struct context *)_ctx;
	bool field[pat->count];
	memset(field, 0, sizeof(field));

	int i;
	int fc = 0;

	for (i=0;i<ctx->number;i++) {
		struct _pattern_field * f = bsearch_pattern(pat, ctx->a[i].wire_id >> 3);
		if (f) {
			int index = f - pat->f;
			if (field[index] == false) {
				field[index] = true;
				++fc;
				if ((f->ctype == CTYPE_ARRAY || f->ctype == CTYPE_PACKED)) {
					struct _pbc_array *array = (struct _pbc_array *)(output + f->offset);
					_pbcA_open(array);
				}
			}
			char * out = (char *)output + f->offset;
			if (unpack_field(f->ctype , f->ptype , ctx->buffer , &ctx->a[i], out) < 0) {
				int j;
				for (j=0;j<pat->count;j++) {
					if (field[j] == true && (pat->f[j].ctype == CTYPE_ARRAY || pat->f[j].ctype == CTYPE_PACKED)) {
						void *array = (char *)output + pat->f[j].offset;
						_pbcA_close(array);
					}
				}
				_pbcC_close(_ctx);
				pat->env->lasterror = "Pattern unpack field error";
				return -i-1;
			}
		}
	}
	_pbcC_close(_ctx);
	if (fc != pat->count) {
		for (i=0;i<pat->count;i++) {
			if (field[i] == false) {
				_pattern_set_default(&pat->f[i], output);
			}
		}
	}
	return 0;
}

/* 
	format : key %type
	%f float
	%F double
	%d int32
	%D int64
	%b bool
	%h int16
	%c int8
	%s slice
	%a array
*/

static int
_ctype(const char * ctype) {
	if (ctype[0]!='%')
		return -1;
	switch (ctype[1]) {
	case 'f':
		return CTYPE_FLOAT;
	case 'F':
		return CTYPE_DOUBLE;
	case 'd':
		return CTYPE_INT32;
	case 'D':
		return CTYPE_INT64;
	case 'b':
		return CTYPE_BOOL;
	case 'h':
		return CTYPE_INT16;
	case 'c':
		return CTYPE_INT8;
	case 's':
		return CTYPE_VAR;
	case 'a':
		return CTYPE_ARRAY;
	default:
		return -1;
	}
}

static int
_ctype_size(const char *ctype) {
	switch (ctype[1]) {
	case 'f':
		return sizeof(float);
	case 'F':
		return sizeof(double);
	case 'd':
		return sizeof(int32_t);
	case 'D':
		return sizeof(int64_t);
	case 'b':
		return sizeof(bool);
	case 'h':
		return sizeof(int16_t);
	case 'c':
		return sizeof(int8_t);
	case 's':
		return sizeof(struct pbc_slice);
	case 'a':
		return sizeof(pbc_array);
	default:
		return 0;
	}
}

static const char *
_copy_string(const char *format , char ** temp) {
	char * output = *temp;
	while (*format == ' ' || *format == '\t' || *format == '\n' || *format == '\r') {
		++format;
	}
	while (*format != '\0' &&
		*format != ' ' &&
		*format != '\t' &&
		*format != '\n' &&
		*format != '\r') {
		*output = *format;
		++output;
		++format;
	}
	*output = '\0';
	++output;
	*temp = output;

	return format;
}

static int
_scan_pattern(const char * format , char * temp) {
	int n = 0;
	for(;;) {
		format = _copy_string(format , &temp);
		if (format[0] == '\0')
			return 0;
		++n;
		format = _copy_string(format , &temp);
		if (format[0] == '\0')
			return n;
	} 
}

static int 
_comp_field(const void * a, const void * b) {
	const struct _pattern_field * fa = a;
	const struct _pattern_field * fb = b;

	return fa->id - fb->id;
}

struct pbc_pattern *
_pbcP_new(struct pbc_env * env, int n) {
	size_t sz = sizeof(struct pbc_pattern) + (sizeof(struct _pattern_field)) * (n-1);
	struct pbc_pattern * ret = malloc(sz);
	memset(ret, 0 , sz);
	ret->count = n;
	ret->env = env;
	return ret;
}

static int
_check_ctype(struct _field * field, struct _pattern_field *f) {
	if (field->label == LABEL_REPEATED) {
		return f->ctype != CTYPE_ARRAY;
	} 
	if (field->label == LABEL_PACKED) {
		return f->ctype != CTYPE_PACKED;
	}
	if (field->type == PTYPE_STRING || field->type == PTYPE_MESSAGE || field->type == PTYPE_BYTES) {
		return f->ctype != CTYPE_VAR;
	}
	if (field->type == PTYPE_FLOAT || field->type == PTYPE_DOUBLE) {
		return !(f->ctype == CTYPE_DOUBLE || f->ctype == CTYPE_FLOAT);
	}
	if (field->type == PTYPE_ENUM) {
		return !(f->ctype == CTYPE_INT8 || 
			f->ctype == CTYPE_INT8 || 
			f->ctype == CTYPE_INT16 ||
			f->ctype == CTYPE_INT32 ||
			f->ctype == CTYPE_INT64);
	}

	return f->ctype == CTYPE_VAR || f->ctype == CTYPE_ARRAY || f->ctype == CTYPE_PACKED ||
		f->ctype == CTYPE_DOUBLE || f->ctype == CTYPE_FLOAT;
}

struct pbc_pattern *
_pattern_new(struct _message *m, const char *format) {
	int len = strlen(format);
	char temp[len+1];
	int n = _scan_pattern(format, temp);
	struct pbc_pattern * pat = _pbcP_new(m->env, n);
	int i;

	const char *ptr = temp;

	int offset = 0;

	for (i=0;i<n;i++) {
		struct _pattern_field * f = &(pat->f[i]);
		struct _field * field = _pbcM_sp_query(m->name, ptr);
		if (field == NULL) {
			m->env->lasterror = "Pattern @new query none exist field";
			goto _error;
		}
		f->id = field->id;
		f->ptype = field->type;
		*f->defv = *field->default_v;
		f->offset = offset;
		f->label = field->label;
		ptr += strlen(ptr) + 1;
		f->ctype = _ctype(ptr);
		if (f->ctype < 0) {
			m->env->lasterror = "Pattern @new use an invalid ctype";
			goto _error;
		}
		
		if (f->ctype == CTYPE_ARRAY && field->label == LABEL_PACKED) {
			f->ctype = CTYPE_PACKED;
		}
		if (_check_ctype(field, f)) {
			m->env->lasterror = "Pattern @new ctype check error";
			goto _error;
		}

		offset += _ctype_size(ptr);
		ptr += strlen(ptr) + 1;
	}

	pat->count = n;

	qsort(pat->f , n , sizeof(struct _pattern_field), _comp_field);

	return pat;
_error:
	free(pat);
	return NULL;
}

struct pbc_pattern * 
pbc_pattern_new(struct pbc_env * env , const char * message, const char * format, ... ) {
	struct _message *m = _pbcP_get_message(env, message);
	if (m==NULL) {
		env->lasterror = "Pattern new can't find proto";
		return NULL;
	}
	if (format[0]=='@') {
		return _pattern_new(m , format+1);
	}

	int len = strlen(format);
	char temp[len+1];
	int n = _scan_pattern(format, temp);
	struct pbc_pattern * pat = _pbcP_new(env, n);
	int i;
	va_list ap;
	va_start(ap , format);

	const char *ptr = temp;

	for (i=0;i<n;i++) {
		struct _pattern_field * f = &(pat->f[i]);
		struct _field * field = _pbcM_sp_query(m->name, ptr);
		if (field == NULL) {
			env->lasterror = "Pattern new query none exist field";
			goto _error;
		}
		f->id = field->id;
		f->ptype = field->type;
		*f->defv = *field->default_v;
		f->offset = va_arg(ap, int);
		f->label = field->label;

		ptr += strlen(ptr) + 1;

		f->ctype = _ctype(ptr);
		if (f->ctype < 0) {
			env->lasterror = "Pattern new use an invalid ctype";
			goto _error;
		}
		if (f->ctype == CTYPE_ARRAY && field->label == LABEL_PACKED) {
			f->ctype = CTYPE_PACKED;
		}
		if (_check_ctype(field, f)) {
			env->lasterror = "Pattern new ctype check error";
			goto _error;
		}

		ptr += strlen(ptr) + 1;
	}

	va_end(ap);

	pat->count = n;

	qsort(pat->f , n , sizeof(struct _pattern_field), _comp_field);

	return pat;
_error:
	free(pat);
	return NULL;
}

void 
pbc_pattern_delete(struct pbc_pattern * pat) {
	free(pat);
}
