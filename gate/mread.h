#ifndef MREAD_H
#define MREAD_H

#include <stdint.h>

struct mread_pool;
 
struct mread_pool * mread_create(uint32_t addr, int port , int max , int buffer);
void mread_close(struct mread_pool *m);
void mread_close_listen(struct mread_pool *self);

int mread_poll(struct mread_pool *m , int timeout);
void * mread_pull(struct mread_pool *m , int size);
void mread_push(struct mread_pool *m, int id, void * buffer, int size, void * ptr);
void mread_yield(struct mread_pool *m);
int mread_closed(struct mread_pool *m);
void mread_close_client(struct mread_pool *m, int id);
int mread_socket(struct mread_pool *m , int index);
void mread_close_listen(struct mread_pool *self);

#endif
