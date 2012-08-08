#include "map.h"

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#define MAX 2000

static void
test(struct map *m) {
	int a[MAX * 2];
	int i;
	int s = 0;
	for (i=0;i<MAX*2;i++) {
		int inc = rand() % 3 + 1;
		s += inc;
		a[i] = s;
	}
	for (i=0;i<MAX * 2;i++) {
		int x = rand()%(MAX*2);
		int y = rand()%(MAX*2);
		int temp = a[x];
		a[x] = a[y];
		a[y] = temp;
	}
	for (i=0;i<MAX;i++) {
		map_insert(m, a[i], i);
	}
	for (i=0;i<MAX;i++) {
		int id = map_search(m,a[i]);
		assert(id == i);
	}
	for (i=0;i<MAX/2;i++) {
		map_erase(m, a[i]);
	}
	for (i=0;i<MAX/2;i++) {
		a[i] = a[i+MAX];
		map_insert(m,a[i],i);
	}
	for (i=0;i<MAX;i++) {
		int id = map_search(m,a[i]);
		assert(id == i);
	}
}

int
main() {
	struct map * m = map_new(MAX);
	test(m);
	map_delete(m);
	return 0;
}
