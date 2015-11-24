#ifndef skynet_hashid_h
#define skynet_hashid_h

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct hashid_node {
	int id;
	struct hashid_node *next;
};

struct hashid {
	int hashmod;
	int cap;
	int count;
	struct hashid_node *id;
	struct hashid_node **hash;
};

static void
hashid_init(struct hashid *hi, int max) {
	int i;
	int hashcap;
	hashcap = 16;
	while (hashcap < max) {
		hashcap *= 2;
	}
	hi->hashmod = hashcap - 1;
	hi->cap = max;
	hi->count = 0;
	hi->id = skynet_malloc(max * sizeof(struct hashid_node));
	for (i=0;i<max;i++) {
		hi->id[i].id = -1;
		hi->id[i].next = NULL;
	}
	hi->hash = skynet_malloc(hashcap * sizeof(struct hashid_node *));
	memset(hi->hash, 0, hashcap * sizeof(struct hashid_node *));
}

static void
hashid_clear(struct hashid *hi) {
	skynet_free(hi->id);
	skynet_free(hi->hash);
	hi->id = NULL;
	hi->hash = NULL;
	hi->hashmod = 1;
	hi->cap = 0;
	hi->count = 0;
}

static int
hashid_lookup(struct hashid *hi, int id) {
	int h = id & hi->hashmod;
	struct hashid_node * c = hi->hash[h];
	while(c) {
		if (c->id == id)
			return c - hi->id;
		c = c->next;
	}
	return -1;
}

static int
hashid_remove(struct hashid *hi, int id) {
	int h = id & hi->hashmod;
	struct hashid_node * c = hi->hash[h];
	if (c == NULL)
		return -1;
	if (c->id == id) {
		hi->hash[h] = c->next;
		goto _clear;
	}
	while(c->next) {
		if (c->next->id == id) {
			struct hashid_node * temp = c->next;
			c->next = temp->next;
			c = temp;
			goto _clear;
		}
		c = c->next;
	}
	return -1;
_clear:
	c->id = -1;
	c->next = NULL;
	--hi->count;
	return c - hi->id;
}

static int
hashid_insert(struct hashid * hi, int id) {
	struct hashid_node *c = NULL;
	int i;
	for (i=0;i<hi->cap;i++) {
		int index = (i+id) % hi->cap;
		if (hi->id[index].id == -1) {
			c = &hi->id[index];
			break;
		}
	}
	assert(c);
	++hi->count;
	c->id = id;
	assert(c->next == NULL);
	int h = id & hi->hashmod;
	if (hi->hash[h]) {
		c->next = hi->hash[h];
	}
	hi->hash[h] = c;
	
	return c - hi->id;
}

static inline int
hashid_full(struct hashid *hi) {
	return hi->count == hi->cap;
}

#endif
