#ifndef sproto_h
#define sproto_h

#include <stddef.h>

struct sproto;
struct sproto_type;

#define SPROTO_REQUEST 0
#define SPROTO_RESPONSE 1

// type (sproto_arg.type)
#define SPROTO_TINTEGER 0
#define SPROTO_TBOOLEAN 1
#define SPROTO_TSTRING 2
#define SPROTO_TSTRUCT 3

// sub type of string (sproto_arg.extra)
#define SPROTO_TSTRING_STRING 0
#define SPROTO_TSTRING_BINARY 1

#define SPROTO_CB_ERROR -1
#define SPROTO_CB_NIL -2
#define SPROTO_CB_NOARRAY -3

struct sproto * sproto_create(const void * proto, size_t sz);
void sproto_release(struct sproto *);

int sproto_prototag(const struct sproto *, const char * name);
const char * sproto_protoname(const struct sproto *, int proto);
// SPROTO_REQUEST(0) : request, SPROTO_RESPONSE(1): response
struct sproto_type * sproto_protoquery(const struct sproto *, int proto, int what);

struct sproto_type * sproto_type(const struct sproto *, const char * type_name);

int sproto_pack(const void * src, int srcsz, void * buffer, int bufsz);
int sproto_unpack(const void * src, int srcsz, void * buffer, int bufsz);

struct sproto_arg {
	void *ud;
	const char *tagname;
	int tagid;
	int type;
	struct sproto_type *subtype;
	void *value;
	int length;
	int index;	// array base 1
	int mainindex;	// for map
	int extra; // SPROTO_TINTEGER: decimal ; SPROTO_TSTRING 0:utf8 string 1:binary
};

typedef int (*sproto_callback)(const struct sproto_arg *args);

int sproto_decode(const struct sproto_type *, const void * data, int size, sproto_callback cb, void *ud);
int sproto_encode(const struct sproto_type *, void * buffer, int size, sproto_callback cb, void *ud);

// for debug use
void sproto_dump(struct sproto *);
const char * sproto_name(struct sproto_type *);

#endif
