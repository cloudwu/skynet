#ifndef MREAD_RINGBUFFER_H
#define MREAD_RINGBUFFER_H

struct ringbuffer;

struct ringbuffer_block {
	int length;
	int offset;
	int id;
	int next;
};

struct ringbuffer * ringbuffer_new(int size);
void ringbuffer_delete(struct ringbuffer * rb);
void ringbuffer_link(struct ringbuffer *rb , struct ringbuffer_block * prev, struct ringbuffer_block * next);
struct ringbuffer_block * ringbuffer_alloc(struct ringbuffer * rb, int size);
int ringbuffer_collect(struct ringbuffer * rb);
void ringbuffer_resize(struct ringbuffer * rb, struct ringbuffer_block * blk, int size);
void ringbuffer_free(struct ringbuffer * rb, struct ringbuffer_block * blk);
int ringbuffer_data(struct ringbuffer * rb, struct ringbuffer_block * blk, int size, int skip, void **ptr);
void * ringbuffer_copy(struct ringbuffer * rb, struct ringbuffer_block * from, int skip, struct ringbuffer_block * to);
struct ringbuffer_block * ringbuffer_yield(struct ringbuffer * rb, struct ringbuffer_block *blk, int skip);

void ringbuffer_dump(struct ringbuffer * rb);

#endif

