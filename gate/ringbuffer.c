#include "ringbuffer.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#define ALIGN(s) (((s) + 3 ) & ~3)

struct ringbuffer {
	int size;
	int head;
};

static inline int
block_offset(struct ringbuffer * rb, struct ringbuffer_block * blk) {
	char * start = (char *)(rb + 1);
	return (char *)blk - start;
}

static inline struct ringbuffer_block *
block_ptr(struct ringbuffer * rb, int offset) {
	char * start = (char *)(rb + 1);
	return (struct ringbuffer_block *)(start + offset);
}

static inline struct ringbuffer_block *
block_next(struct ringbuffer * rb, struct ringbuffer_block * blk) {
	int align_length = ALIGN(blk->length);
	int head = block_offset(rb, blk);
	if (align_length + head == rb->size) {
		return NULL;
	}
	assert(align_length + head < rb->size);
	return block_ptr(rb, head + align_length);
}

struct ringbuffer *
ringbuffer_new(int size) {
	struct ringbuffer * rb = malloc(sizeof(*rb) + size);
	rb->size = size;
	rb->head = 0;
	struct ringbuffer_block * blk = block_ptr(rb, 0);
	blk->length = size;
	blk->id = -1;
	return rb;
}

void
ringbuffer_delete(struct ringbuffer * rb) {
	free(rb);
}

void
ringbuffer_link(struct ringbuffer *rb , struct ringbuffer_block * head, struct ringbuffer_block * next) {
	while (head->next >=0) {
		head = block_ptr(rb, head->next);
	}
	next->id = head->id;
	head->next = block_offset(rb, next);
}

static struct ringbuffer_block *
_alloc(struct ringbuffer * rb, int total_size , int size) {
	struct ringbuffer_block * blk = block_ptr(rb, rb->head);
	int align_length = ALIGN(sizeof(struct ringbuffer_block) + size);
	blk->length = sizeof(struct ringbuffer_block) + size;
	blk->offset = 0;
	blk->next = -1;
	blk->id = -1;
	struct ringbuffer_block * next = block_next(rb, blk);
	rb->head = block_offset(rb, next);
	if (align_length < total_size) {
		next->length = total_size - align_length;
		if (next->length >= sizeof(struct ringbuffer_block)) {
			next->id = -1;
		}
	}
	return blk;
}

struct ringbuffer_block *
ringbuffer_alloc(struct ringbuffer * rb, int size) {
	int align_length = ALIGN(sizeof(struct ringbuffer_block) + size);
	int i;
	for (i=0;i<2;i++) {
		int free_size = 0;
		struct ringbuffer_block * blk = block_ptr(rb, rb->head);
		do {
			if (blk->length >= sizeof(struct ringbuffer_block) && blk->id >= 0)
				return NULL;
			free_size += ALIGN(blk->length);
			if (free_size >= align_length) {
				return _alloc(rb, free_size , size);
			}
			blk = block_next(rb, blk);
		} while(blk);
		rb->head = 0;
	}
	return NULL;
}

static int
_last_id(struct ringbuffer * rb) {
	int i;
	for (i=0;i<2;i++) {
		struct ringbuffer_block * blk = block_ptr(rb, rb->head);
		do {
			if (blk->length >= sizeof(struct ringbuffer_block) && blk->id >= 0)
				return blk->id;
			blk = block_next(rb, blk);
		} while(blk);
		rb->head = 0;
	}
	return -1;
}

int
ringbuffer_collect(struct ringbuffer * rb) {
	int id = _last_id(rb);
	struct ringbuffer_block *blk = block_ptr(rb, 0);
	do {
		if (blk->length >= sizeof(struct ringbuffer_block) && blk->id == id) {
			blk->id = -1;
		}
		blk = block_next(rb, blk);
	} while(blk);
	return id;
}

void
ringbuffer_resize(struct ringbuffer * rb, struct ringbuffer_block * blk, int size) {
	if (size == 0) {
		rb->head = block_offset(rb, blk);
		return;
	}
	int align_length = ALIGN(sizeof(struct ringbuffer_block) + size);
	int old_length = ALIGN(blk->length);
	assert(align_length < old_length);
	blk->length = size + sizeof(struct ringbuffer_block);
	if (align_length == old_length) {
		return;
	}
	blk = block_next(rb, blk);
	blk->length = old_length - align_length;
	if (blk->length >= sizeof(struct ringbuffer_block)) {
		blk->id = -1;
	}
	rb->head = block_offset(rb, blk);
}

static int
_block_id(struct ringbuffer_block * blk) {
	assert(blk->length >= sizeof(struct ringbuffer_block));
	int id = blk->id;
	assert(id>=0);
	return id;
}

void
ringbuffer_free(struct ringbuffer * rb, struct ringbuffer_block * blk) {
	if (blk == NULL)
		return;
	int id = _block_id(blk);
	blk->id = -1;
	while (blk->next >= 0) {
		blk = block_ptr(rb, blk->next);
		assert(_block_id(blk) == id);
		blk->id = -1;
	}
}

int
ringbuffer_data(struct ringbuffer * rb, struct ringbuffer_block * blk, int size, int skip, void **ptr) {
	int length = blk->length - sizeof(struct ringbuffer_block) - blk->offset;
	for (;;) {
		if (length > skip) {
			if (length - skip >= size) {
				char * start = (char *)(blk + 1);
				*ptr = (start + blk->offset + skip);
				return size;
			}
			*ptr = NULL;
			int ret = length - skip;
			while (blk->next >= 0) {
				blk = block_ptr(rb, blk->next);
				ret += blk->length - sizeof(struct ringbuffer_block);
				if (ret >= size)
					return size;
			}
			return ret;
		}
		if (blk->next < 0) {
			assert(length == skip);
			*ptr = NULL;
			return 0;
		}
		blk = block_ptr(rb, blk->next);
		assert(blk->offset == 0);
		skip -= length;
		length = blk->length - sizeof(struct ringbuffer_block);
	}
}

void *
ringbuffer_copy(struct ringbuffer * rb, struct ringbuffer_block * from, int skip, struct ringbuffer_block * to) {
	int size = to->length - sizeof(struct ringbuffer_block);
	int length = from->length - sizeof(struct ringbuffer_block) - from->offset;
	char * ptr = (char *)(to+1);
	for (;;) {
		if (length > skip) {
			char * src = (char *)(from + 1);
			src += from->offset + skip;
			length -= skip;
			while (length < size) {
				memcpy(ptr, src, length);
				assert(from->next >= 0);
				from = block_ptr(rb , from->next);
				assert(from->offset == 0);
				ptr += length;
				size -= length;
				length = from->length - sizeof(struct ringbuffer_block);
				src =  (char *)(from + 1);
			}
			memcpy(ptr, src , size);
			to->id = from->id;
			return (char *)(to + 1);
		}
		assert(from->next >= 0);
		from = block_ptr(rb, from->next);
		assert(from->offset == 0);
		skip -= length;
		length = from->length - sizeof(struct ringbuffer_block);
	}
}

struct ringbuffer_block *
ringbuffer_yield(struct ringbuffer * rb, struct ringbuffer_block *blk, int skip) {
	int length = blk->length - sizeof(struct ringbuffer_block) - blk->offset;
	for (;;) {
		if (length > skip) {
			blk->offset += skip;
			return blk;
		}
		blk->id = -1;
		if (blk->next < 0) {
			return NULL;
		}
		blk = block_ptr(rb, blk->next);
		assert(blk->offset == 0);
		skip -= length;
		length = blk->length - sizeof(struct ringbuffer_block);
	}
}

void 
ringbuffer_dump(struct ringbuffer * rb) {
	struct ringbuffer_block *blk = block_ptr(rb,0);
	int i=0;
	printf("total size= %d\n",rb->size);
	while (blk) {
		++i;
		if (i>10)
			break;
		if (blk->length >= sizeof(*blk)) {
			printf("[%u : %d]", (unsigned)(blk->length - sizeof(*blk)), block_offset(rb,blk));
			printf(" id=%d",blk->id);
			if (blk->id >=0) {
				printf(" offset=%d next=%d",blk->offset, blk->next);
			}
		} else {
			printf("<%u : %d>", blk->length, block_offset(rb,blk));
		}
		printf("\n");
		blk = block_next(rb, blk);
	}
}
