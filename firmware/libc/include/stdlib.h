#ifndef _POCKET_STDLIB_H
#define _POCKET_STDLIB_H
#include <stddef.h>
int   abs(int v);
long  labs(long v);
int   atoi(const char *s);
long  strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
char *getenv(const char *name);
void  abort(void) __attribute__((noreturn));
void  exit(int code) __attribute__((noreturn));
void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void *realloc(void *p, size_t n);
void  free(void *p);
#endif
