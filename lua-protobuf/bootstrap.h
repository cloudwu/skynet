#ifndef PROTOBUF_C_BOOTSTRAP_H
#define PROTOBUF_C_BOOTSTRAP_H

#include "proto.h"
#include "pbc.h"

void _pbcB_init(struct pbc_env *);
void _pbcB_register_fields(struct pbc_env *, pbc_array queue);

#endif
