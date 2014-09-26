#ifndef sproto_h
#define sproto_h

#include <stddef.h>

struct sproto;
struct sproto_type;

#define SPROTO_REQUEST 0
#define SPROTO_RESPONSE 1

#define SPROTO_TINTEGER 0
#define SPROTO_TBOOLEAN 1
#define SPROTO_TSTRING 2
#define SPROTO_TSTRUCT 3

struct sproto * sproto_create(const void * proto, size_t sz);
void sproto_release(struct sproto *);

int sproto_prototag(struct sproto *, const char * name);
const char * sproto_protoname(struct sproto *, int proto);
// SPROTO_REQUEST(0) : request, SPROTO_RESPONSE(1): response
struct sproto_type * sproto_protoquery(struct sproto *, int proto, int what);

struct sproto_type * sproto_type(struct sproto *, const char * type_name);

int sproto_pack(const void * src, int srcsz, void * buffer, int bufsz);
int sproto_unpack(const void * src, int srcsz, void * buffer, int bufsz);

typedef int (*sproto_callback)(void *ud, const char *tagname, int type, int index, struct sproto_type *, void *value, int length);

int sproto_decode(struct sproto_type *, const void * data, int size, sproto_callback cb, void *ud);
int sproto_encode(struct sproto_type *, void * buffer, int size, sproto_callback cb, void *ud);

// for debug use
void sproto_dump(struct sproto *);
const char * sproto_name(struct sproto_type *);

#endif
