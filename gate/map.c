#include "map.h"

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

struct node {
	int fd;
	int id;
	struct node * next;
};

struct map {
	int size;
	struct node * hash;
};

struct map * 
map_new(int max) {
	int sz = 1;
	while (sz <= max) {
		sz *= 2;
	}
	struct map * m = malloc(sizeof(*m));
	m->size = sz;
	m->hash = malloc(sizeof(struct node) * sz);
	int i;
	for (i=0;i<sz;i++) {
		m->hash[i].fd = -1;
		m->hash[i].id = 0;
		m->hash[i].next = NULL;
	}
	return m;
}

void 
map_delete(struct map * m) {
	free(m->hash);
	free(m);
}

int 
map_search(struct map * m, int fd) {
	int hash = fd & (m->size-1);
	struct node * n = &m->hash[hash];
	do {
		if (n->fd == fd)
			return n->id;
		n = n->next;
	} while(n);
	return -1;
}

void 
map_insert(struct map * m, int fd, int id) {
	int hash = fd & (m->size-1);
	struct node * n = &m->hash[hash];
	if (n->fd < 0) {
		n->fd = fd;
		n->id = id;
		return;
	}
	int ohash = n->fd & (m->size-1);
	if (hash != ohash) {
		struct node * last = &m->hash[ohash];
		while (last->next != &m->hash[hash]) {
			last = last->next;
		}
		last->next = n->next;

		int ofd = n->fd;
		int oid = n->id;
		n->fd = fd;
		n->id = id;
		n->next = NULL;
		map_insert(m,ofd, oid);
		return;
	}

	int last = (n - m->hash) * 2;
	int i;
	for (i=0;i<m->size;i++) {
		int idx = (i + last + 1) & (m->size - 1);
		struct node * temp = &m->hash[idx];
		if (temp->fd < 0) {
			temp->fd = fd;
			temp->id = id;
			temp->next = n->next;
			n->next = temp;
			return;
		}
	}
	assert(0);
}

void
map_erase(struct map *m , int fd) {
	int hash = fd & (m->size-1);
	struct node * n = &m->hash[hash];
	if (n->fd == fd) {
		if (n->next == NULL) {
			n->fd = -1;
			return;
		}
		struct node * next = n->next;
		n->fd = next->fd;
		n->id = next->id;
		n->next = next->next;
		next->fd = -1;
		next->next = NULL;
		return;
	}
	if (n->next == NULL) {
		return;
	}
	struct node * last = n;
	n = n->next;
	for(;;) {
		if (n->fd == fd) {
			n->fd = -1;
			last->next = n->next;
			n->next = NULL;
			return;
		}
		if (n->next == NULL)
			return;
		last = n;
		n = n->next;
	}
}

void
map_dump(struct map *m) {
	int i;
	for (i=0;i<m->size;i++) {
		struct node * n = &(m->hash[i]);
		printf("[%d] fd = %d , id = %d , next = %d\n",i,n->fd,n->id,(int)(n->next - m->hash));
	}
}