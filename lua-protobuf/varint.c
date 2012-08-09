#include "varint.h"

#include "pbc.h"

#include <stdint.h>

inline int
_pbcV_encode32(uint32_t number, uint8_t buffer[10])
{
	if (number < 0x80) {
		buffer[0] = (uint8_t) number ; 
		return 1;
	}
	buffer[0] = (uint8_t) (number | 0x80 );
	if (number < 0x4000) {
		buffer[1] = (uint8_t) (number >> 7 );
		return 2;
	}
	buffer[1] = (uint8_t) ((number >> 7) | 0x80 );
	if (number < 0x200000) {
		buffer[2] = (uint8_t) (number >> 14);
		return 3;
	}
	buffer[2] = (uint8_t) ((number >> 14) | 0x80 );
	if (number < 0x10000000) {
		buffer[3] = (uint8_t) (number >> 21);
		return 4;
	}
	buffer[3] = (uint8_t) ((number >> 21) | 0x80 );
	buffer[4] = (uint8_t) (number >> 28);
	return 5;
}

int
_pbcV_encode(uint64_t number, uint8_t buffer[10]) 
{
	if ((number & 0xffffffff) == number) {
		return _pbcV_encode32((uint32_t)number , buffer);
	}
	int i = 0;
	do {
		buffer[i] = (uint8_t)(number | 0x80);
		number >>= 7;
		++i;
	} while (number >= 0x80);
	buffer[i] = (uint8_t)number;
	return i+1;
}

int
_pbcV_decode(uint8_t buffer[10], struct longlong *result) {
	if (!(buffer[0] & 0x80)) {
		result->low = buffer[0];
		result->hi = 0;
		return 1;
	}
	uint32_t r = buffer[0] & 0x7f;
	int i;
	for (i=1;i<4;i++) {
		r |= ((buffer[i]&0x7f) << (7*i));
		if (!(buffer[i] & 0x80)) {
			result->low = r;
			result->hi = 0;
			return i+1;
		}
	}
	uint64_t lr = 0;
	for (i=4;i<10;i++) {
		lr |= ((buffer[i] & 0x7f) << (7*(i-4)));
		if (!(buffer[i] & 0x80)) {
			result->hi = (uint32_t)(lr >> 4);
			result->low = r | (((uint32_t)lr & 0xf) << 28);
			return i+1;
		}
	}

	result->low = 0;
	result->hi = 0;
	return 10;
}

int 
_pbcV_zigzag32(int32_t n, uint8_t buffer[10])
{
	n = (n << 1) ^ (n >> 31);
	return _pbcV_encode32(n,buffer);
}

int 
_pbcV_zigzag(int64_t n, uint8_t buffer[10])
{
	n = (n << 1) ^ (n >> 63);
	return _pbcV_encode(n,buffer);
}

void
_pbcV_dezigzag64(struct longlong *r)
{
	uint32_t low = r->low;
	r->low = ((low >> 1) | ((r->hi & 1) << 31)) ^ - (low & 1);
	r->hi = (r->hi >> 1) ^ - (low & 1);
}

void
_pbcV_dezigzag32(struct longlong *r)
{
	uint32_t low = r->low;
	r->low = (low >> 1) ^ - (low & 1);
	r->hi = -(low >> 31);
}
