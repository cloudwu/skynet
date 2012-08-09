#ifndef SKYNET_ENV_H
#define SKYNET_ENV_H

const char * skynet_getenv(const char *key);
void skynet_setenv(const char *key, const char *value);

void skynet_env_init();

#endif
