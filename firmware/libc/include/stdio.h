#ifndef _POCKET_STDIO_H
#define _POCKET_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct _pocket_file FILE;
extern FILE *stderr, *stdout;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
int   printf(const char *fmt, ...);
int   fprintf(FILE *f, const char *fmt, ...);
int   vfprintf(FILE *f, const char *fmt, va_list ap);
int   snprintf(char *buf, size_t n, const char *fmt, ...);
int   vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int   sprintf(char *buf, const char *fmt, ...);
int   puts(const char *s);
int   putchar(int c);
int   fputc(int c, FILE *f);
int   fflush(FILE *f);
FILE *fopen(const char *name, const char *mode);
size_t fwrite(const void *p, size_t s, size_t n, FILE *f);
size_t fread(void *p, size_t s, size_t n, FILE *f);
int   fclose(FILE *f);
#endif
