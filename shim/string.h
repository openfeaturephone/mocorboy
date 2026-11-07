#ifndef GBC_SHIM_STRING_H
#define GBC_SHIM_STRING_H
#include <stddef.h>
void *memcpy(void *d, const void *s, size_t n); /* rt.c */
void *memset(void *d, int c, size_t n);         /* rt.c */
#endif
