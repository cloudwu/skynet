#ifndef MREAD_MAP_H
#define MREAD_MAP_H

struct map;

struct map * map_new(int max);
void map_delete(struct map *);
int map_search(struct map * , int fd);
void map_insert(struct map * , int fd, int id);
void map_erase(struct map *, int fd);
void map_dump(struct map *m);

#endif
