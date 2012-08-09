#ifndef PROTOBUF_C_VARINT_H
#define PROTOBUF_C_VARINT_H

#include <stdint.h>

struct longlong {
	uint32_t low;
	uint32_t hi;
};

int _pbcV_encode32(uint32_t number, uint8_t buffer[10]);
int _pbcV_encode(uint64_t number, uint8_t buffer[10]);
int _pbcV_zigzag32(int32_t number, uint8_t buffer[10]);
int _pbcV_zigzag(int64_t number, uint8_t buffer[10]);

int _pbcV_decode(uint8_t buffer[10], struct longlong *result);
void _pbcV_dezigzag64(struct longlong *r);
void _pbcV_dezigzag32(struct longlong *r);

#endif
