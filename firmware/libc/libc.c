/* The little of the C library the game code needs, for the bare-metal
 * Pocket firmware.  Output (fprintf to stderr, the port's debug messages)
 * goes to the SoC's debug register, which the simulation prints and the
 * hardware ignores. */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../pocket_hw.h"

/* ---- memory and strings ------------------------------------------- */
void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *dp = d;
    const uint8_t *sp = s;
    if ((((uintptr_t)dp | (uintptr_t)sp) & 3) == 0) {
        while (n >= 16) {
            uint32_t a = ((const uint32_t *)sp)[0], b = ((const uint32_t *)sp)[1];
            uint32_t c = ((const uint32_t *)sp)[2], e = ((const uint32_t *)sp)[3];
            ((uint32_t *)dp)[0] = a; ((uint32_t *)dp)[1] = b;
            ((uint32_t *)dp)[2] = c; ((uint32_t *)dp)[3] = e;
            dp += 16; sp += 16; n -= 16;
        }
        while (n >= 4) { *(uint32_t *)dp = *(const uint32_t *)sp; dp += 4; sp += 4; n -= 4; }
    }
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *dp = d;
    const uint8_t *sp = s;
    if (dp <= sp || dp >= sp + n) return memcpy(d, s, n);
    dp += n; sp += n;
    while (n--) *--dp = *--sp;
    return d;
}

void *memset(void *d, int c, size_t n)
{
    uint8_t *dp = d;
    uint32_t w = (uint8_t)c * 0x01010101u;
    while (n && ((uintptr_t)dp & 3)) { *dp++ = (uint8_t)c; n--; }
    while (n >= 16) {
        ((uint32_t *)dp)[0] = w; ((uint32_t *)dp)[1] = w;
        ((uint32_t *)dp)[2] = w; ((uint32_t *)dp)[3] = w;
        dp += 16; n -= 16;
    }
    while (n >= 4) { *(uint32_t *)dp = w; dp += 4; n -= 4; }
    while (n--) *dp++ = (uint8_t)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}

size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return (uint8_t)*a - (uint8_t)*b;
}
int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) { if (*a != *b || !*a) return (uint8_t)*a - (uint8_t)*b; }
    return 0;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    for (; n && *s; n--) *d++ = *s++;
    while (n--) *d++ = 0;
    return r;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strchr(const char *s, int c)
{
    for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return NULL; }
}
char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; }
}
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int strcasecmp(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) a++, b++;
    return lower((uint8_t)*a) - lower((uint8_t)*b);
}
int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++)
        if (lower(*a) != lower(*b) || !*a) return lower((uint8_t)*a) - lower((uint8_t)*b);
    return 0;
}

/* ---- stdlib ------------------------------------------------------- */
int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }

unsigned long strtoul(const char *s, char **end, int base)
{
    unsigned long v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] | 32) == 'x') { s += 2; base = 16; }
    if (base == 0) base = 10;
    for (;; s++) {
        int d = *s >= '0' && *s <= '9' ? *s - '0' : (*s | 32) >= 'a' && (*s | 32) <= 'z' ? (*s | 32) - 'a' + 10 : 99;
        if (d >= base) break;
        v = v * (unsigned long)base + (unsigned long)d;
    }
    if (end) *end = (char *)s;
    return v;
}
long strtol(const char *s, char **end, int base)
{
    int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    return neg ? -(long)strtoul(s, end, base) : (long)strtoul(s, end, base);
}
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }

/* the port's test switches: the simulation build replays the attract demo
 * and ends when it is finished (render.c) */
char *getenv(const char *name)
{
#ifdef POCKET_SIM
    if (strcmp(name, "SKY_DEMO_TEST") == 0) return "1";
#endif
    (void)name;
    return NULL;
}

void plat_halt(int code);
void exit(int code) { plat_halt(code); for (;;) {} }
void abort(void) { plat_halt(134); for (;;) {} }

/* a bump allocator: nothing on the Pocket frees memory in a loop */
extern char _end[], _heap_end[];
static char *s_brk;
void *malloc(size_t n)
{
    char *p;
    if (!s_brk) s_brk = (char *)(((uintptr_t)_end + 15) & ~(uintptr_t)15);
    n = (n + 15) & ~(size_t)15;
    if (s_brk + n > _heap_end) return NULL;
    p = s_brk;
    s_brk += n;
    return p;
}
/* the heap's end so far (save states) */
char *libc_brk(void) { return s_brk ? s_brk : (char *)(((uintptr_t)_end + 15) & ~(uintptr_t)15); }
void *calloc(size_t n, size_t m) { void *p = malloc(n * m); if (p) memset(p, 0, n * m); return p; }
void *realloc(void *p, size_t n) { void *q = malloc(n); if (q && p) memcpy(q, p, n); return q; }
void free(void *p) { (void)p; }

/* ---- stdio: formatted output to the debug register ---------------- */
struct _pocket_file { int fd; };
static struct _pocket_file s_err = { 2 }, s_out = { 1 };
FILE *stderr = &s_err, *stdout = &s_out;

typedef struct { char *buf; size_t n, len; } Out;

static void out_c(Out *o, char c)
{
    if (o->buf) { if (o->len + 1 < o->n) o->buf[o->len] = c; }
    else hw_wr(R_DEBUG_PUTC, (uint8_t)c);
    o->len++;
}

static void out_num(Out *o, unsigned long v, int base, int neg, int width, char pad, int upper)
{
    char tmp[24];
    int i = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { tmp[i++] = dig[v % (unsigned)base]; v /= (unsigned)base; } while (v);
    if (neg) tmp[i++] = '-';
    while (i < width) tmp[i++] = pad;
    while (i) out_c(o, tmp[--i]);
}

static int vformat(Out *o, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        int width = 0, lng = 0;
        char pad = ' ';
        if (*fmt != '%') { out_c(o, *fmt); continue; }
        fmt++;
        if (*fmt == '-') fmt++;
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h') { if (*fmt == 'l') lng++; fmt++; }
        switch (*fmt) {
            case 'd': case 'i': {
                long v = lng ? va_arg(ap, long) : va_arg(ap, int);
                out_num(o, (unsigned long)(v < 0 ? -v : v), 10, v < 0, width, pad, 0);
                break;
            }
            case 'u': out_num(o, lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 10, 0, width, pad, 0); break;
            case 'x': out_num(o, lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16, 0, width, pad, 0); break;
            case 'X': out_num(o, lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16, 0, width, pad, 1); break;
            case 'p': out_c(o, '0'); out_c(o, 'x'); out_num(o, (uintptr_t)va_arg(ap, void *), 16, 0, 8, '0', 0); break;
            case 'c': out_c(o, (char)va_arg(ap, int)); break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                int n = 0;
                if (!s) s = "(null)";
                while (s[n]) n++;
                while (n++ < width) out_c(o, ' ');
                while (*s) out_c(o, *s++);
                break;
            }
            case '%': out_c(o, '%'); break;
            case 0: return (int)o->len;
            default: out_c(o, '%'); out_c(o, *fmt); break;
        }
    }
    return (int)o->len;
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    Out o = { buf, n, 0 };
    int r = vformat(&o, fmt, ap);
    if (buf && n) buf[o.len < n ? o.len : n - 1] = 0;
    return r;
}
int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt); r = vsnprintf(buf, n, fmt, ap); va_end(ap);
    return r;
}
int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt); r = vsnprintf(buf, (size_t)-1 >> 1, fmt, ap); va_end(ap);
    return r;
}
int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    Out o = { NULL, 0, 0 };
    (void)f;
    return vformat(&o, fmt, ap);
}
int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt); r = vfprintf(f, fmt, ap); va_end(ap);
    return r;
}
int printf(const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt); r = vfprintf(stdout, fmt, ap); va_end(ap);
    return r;
}
int fputc(int c, FILE *f) { (void)f; hw_wr(R_DEBUG_PUTC, (uint8_t)c); return c; }
int putchar(int c) { return fputc(c, stdout); }
int puts(const char *s) { while (*s) putchar(*s++); putchar('\n'); return 0; }
int fflush(FILE *f) { (void)f; return 0; }
FILE *fopen(const char *name, const char *mode) { (void)name; (void)mode; return NULL; }
size_t fwrite(const void *p, size_t s, size_t n, FILE *f) { (void)p; (void)s; (void)f; return n; }
size_t fread(void *p, size_t s, size_t n, FILE *f) { (void)p; (void)s; (void)n; (void)f; return 0; }
int fclose(FILE *f) { (void)f; return 0; }

/* ---- 64-bit division (RV32 has none) ------------------------------ */
uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0, r = 0;
    int i;
    if (d == 0) { if (rem) *rem = n; return ~(uint64_t)0; }
    if ((n >> 32) == 0 && (d >> 32) == 0) {
        if (rem) *rem = (uint32_t)n % (uint32_t)d;
        return (uint32_t)n / (uint32_t)d;
    }
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= (uint64_t)1 << i; }
    }
    if (rem) *rem = r;
    return q;
}
uint64_t __udivdi3(uint64_t n, uint64_t d) { return __udivmoddi4(n, d, NULL); }
uint64_t __umoddi3(uint64_t n, uint64_t d) { uint64_t r; __udivmoddi4(n, d, &r); return r; }
int64_t __divdi3(int64_t n, int64_t d)
{
    int neg = (n < 0) ^ (d < 0);
    uint64_t q = __udivmoddi4(n < 0 ? -(uint64_t)n : (uint64_t)n, d < 0 ? -(uint64_t)d : (uint64_t)d, NULL);
    return neg ? -(int64_t)q : (int64_t)q;
}
int64_t __moddi3(int64_t n, int64_t d)
{
    uint64_t r;
    __udivmoddi4(n < 0 ? -(uint64_t)n : (uint64_t)n, d < 0 ? -(uint64_t)d : (uint64_t)d, &r);
    return n < 0 ? -(int64_t)r : (int64_t)r;
}
