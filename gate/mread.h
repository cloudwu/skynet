#ifndef MREAD_H
#define MREAD_H

struct mread_pool;
 
struct mread_pool * mread_create(int port , int max , int buffer);
void mread_close(struct mread_pool *m);

int mread_poll(struct mread_pool *m , int timeout);
void * mread_pull(struct mread_pool *m , int size);
void mread_yield(struct mread_pool *m);
int mread_closed(struct mread_pool *m);
void mread_close_client(struct mread_pool *m, int id);
int mread_socket(struct mread_pool *m , int index);

#endif
