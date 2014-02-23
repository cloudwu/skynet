#ifndef skynet_timing_queue_h
#define skynet_timing_queue_h

struct tqueue * tqueue_new();
void tqueue_delete(struct tqueue *);

void tqueue_push(struct tqueue *, int session, double time);
double tqueue_pop(struct tqueue *, int session);

// for debug
void tqueue_dump(struct tqueue *tq);

#endif
