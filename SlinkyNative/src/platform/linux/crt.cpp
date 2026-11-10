// C runtime entry points the client calls by their address in the original image, bound to the process's real C
// library. This is the Linux counterpart of src/platform/win/crt.cpp; the names are the same ones the recovered
// code already calls, so nothing in src/engine, src/logic or src/modules needs a platform branch.
//
// The two wide formatting entry points are hand-rolled here rather than bound to libc: wchar_t is 2 bytes in
// this build (-fshort-wchar, as the image's UTF-16 paths require), so libc's wcscall set, which reads 4-byte
// units, would not be looking at the same strings.
#define _GNU_SOURCE 1
#include <math.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

extern "C" {
// printf options word: kept for the Windows counterpart's signature, the formatting below ignores it
static unsigned long long printf_options;
unsigned long long *printf_options_ptr() { return &printf_options; }

// ---- memory ----
void *libc_calloc(size_t n, size_t size) { return calloc(n, size); }
void libc_free(void *ptr) { free(ptr); }
void *libc_malloc(size_t n) { return malloc(n); }
void *libc_realloc(void *ptr, size_t n) { return realloc(ptr, n); }
void *libc_memcpy(void *dest, const void *src, size_t n) { return memcpy(dest, src, n); }
void *libc_memcpy_alt(void *dest, const void *src, size_t n) { return memcpy(dest, src, n); }
void *libc_memset(void *ptr, int c, size_t n) { return memset(ptr, c, n); }
void libc_memmove(void *dest, const void *src, size_t n) { memmove(dest, src, n); }

// ---- strings ----
size_t libc_strlen(const char *str) { return strlen(str); }
int libc_strcmp(const char *left, const char *right) { return strcmp(left, right); }
int libc_strncmp(const char *left, const char *right, size_t n) { return strncmp(left, right, n); }
int libc_strncmp_alt(const char *left, const char *right, size_t n) { return strncmp(left, right, n); }
// a UTF-16 length, not a libc wcslen: the strings here are 2-byte units
size_t libc_wcslen(const wchar_t *str) {
    size_t n = 0;
    while (str[n]) n++;
    return n;
}
// The one secure-CRT entry point the image calls by its own name: MSVC ships it, libc has no counterpart,
// and the units copied are the 2-byte ones every other wide string in this build uses.
int wcsncpy_s(wchar_t *dst, size_t dst_size, const wchar_t *src, size_t count) {
    if (!dst || !src || dst_size == 0) return EINVAL;
    size_t n = 0;
    while (n < count && src[n]) n++;
    if (n >= dst_size) {
        dst[0] = 0;
        return ERANGE;
    }
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    dst[n] = 0;
    return 0;
}

// ---- narrow formatting: the C library's own, with the failures the callers already check for ----
int libc_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    int r = vsnprintf(buf, n, fmt, ap);
    return r < 0 ? -1 : r;
}
int libc_snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = libc_vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}
// the module code reaches snprintf through this name (include/slinky/api.h)
int log_fmt(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int result = libc_vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return result;
}
int libc_sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return r < 0 ? -1 : r;
}

// ---- wide formatting, on 2-byte units ----
// %s takes a wide argument, %S a narrow one (the MSVC reading of the image's "%s\%S" and "%S" formats); every
// other conversion is produced by snprintf on a single-specifier format and then converted, so the number
// formatting stays the C library's. An unknown specifier is a failure, not a guess.
static int wide_append(wchar_t *buf, size_t cap, size_t *o, const char *narrow) {
    for (size_t i = 0; narrow[i];) {
        unsigned char c = (unsigned char)narrow[i];
        unsigned cp;
        size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1fu; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0fu; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; len = 4; }
        else return -1;   // not a UTF-8 lead byte
        if (!narrow[i + 1] && len > 1) return -1;   // sequence cut short by the terminator
        for (size_t k = 1; k < len; k++) {
            if (((unsigned char)narrow[i + k] & 0xC0) != 0x80) return -1;
            cp = cp << 6 | ((unsigned)narrow[i + k] & 0x3fu);
        }
        i += len;
        if (cp > 0xFFFF) {
            if (*o + 2 >= cap) return -1;
            cp -= 0x10000;
            buf[(*o)++] = (wchar_t)(0xD800 + (cp >> 10));
            buf[(*o)++] = (wchar_t)(0xDC00 + (cp & 0x3ff));
        } else {
            if (*o + 1 >= cap) return -1;
            buf[(*o)++] = (wchar_t)cp;
        }
    }
    return 0;
}

int libc_vswprintf(unsigned long long opts, wchar_t *buf, size_t n, const wchar_t *fmt, void *locale, va_list ap) {
    (void)opts; (void)locale;
    if (!buf || !fmt) return -1;
    size_t o = 0;
    for (size_t i = 0; fmt[i];) {
        wchar_t c = fmt[i];
        if (c != L'%') {
            if (o + 1 >= n) return -1;
            buf[o++] = c;
            i++;
            continue;
        }
        i++;
        if (fmt[i] == L'%') { if (o + 1 >= n) return -1; buf[o++] = L'%'; i++; continue; }
        char spec[32];
        size_t s = 0;
        spec[s++] = '%';
        while (fmt[i] && (fmt[i] == L'-' || fmt[i] == L'+' || fmt[i] == L' ' || fmt[i] == L'#' || fmt[i] == L'0'))
            spec[s++] = (char)fmt[i++];
        while (fmt[i] >= L'0' && fmt[i] <= L'9') spec[s++] = (char)fmt[i++];
        if (fmt[i] == L'.') { spec[s++] = '.'; i++; while (fmt[i] >= L'0' && fmt[i] <= L'9') spec[s++] = (char)fmt[i++]; }
        wchar_t conv = fmt[i];
        if (conv == L'h' || conv == L'l' || conv == L'j' || conv == L'z')
            return -1;   // a length modifier: none of the image's wide formats has one, and guessing the
                         // argument width from the specifier is exactly the kind of thing that corrupts a stack
        i++;
        if (!conv) return -1;   // the format ends inside a specifier
        if (conv == L's' || conv == L'S') {
            // %s: a wide argument. %S: a narrow one.
            if (conv == L's') {
                const wchar_t *w = va_arg(ap, const wchar_t *);
                if (!w) w = L"(null)";
                char tmp[512];
                size_t k = 0;
                for (; w[k] && k + 1 < sizeof tmp; k++) tmp[k] = (char)w[k];   // the paths this formats are ASCII
                tmp[k] = 0;
                if (wide_append(buf, n, &o, tmp)) return -1;
            } else {
                const char *a = va_arg(ap, const char *);
                if (wide_append(buf, n, &o, a ? a : "(null)")) return -1;
            }
            continue;
        }
        int widen = 0;   // integers are read as int and printed as long, so the specifier has to say so
        switch (conv) {
        case L'd': case L'i': case L'u': case L'x': case L'X': case L'o': widen = 1; break;
        case L'f': case L'F': case L'e': case L'E': case L'g': case L'G': case L'c': case L'p': break;
        default: return -1;   // a specifier the recovered formats do not use
        }
        spec[s++] = (char)conv;
        if (widen) spec[s++] = 'l';
        spec[s] = 0;
        char out[512];
        switch (conv) {
        case L'd': case L'i': {
            if (snprintf(out, sizeof out, spec, va_arg(ap, int)) < 0) return -1;
            break;
        }
        case L'u': case L'x': case L'X': case L'o': {
            if (snprintf(out, sizeof out, spec, va_arg(ap, unsigned)) < 0) return -1;
            break;
        }
        case L'f': case L'F': case L'e': case L'E': case L'g': case L'G': {
            double v = va_arg(ap, double);
            if (snprintf(out, sizeof out, spec, v) < 0) return -1;
            break;
        }
        case L'c': {
            if (snprintf(out, sizeof out, spec, va_arg(ap, int)) < 0) return -1;
            break;
        }
        default: {   // %p
            if (snprintf(out, sizeof out, spec, va_arg(ap, void *)) < 0) return -1;
            break;
        }
        }
        if (wide_append(buf, n, &o, out)) return -1;
    }
    buf[o] = 0;
    return (int)o;
}

int libc_vswprintf_s(unsigned long long opts, wchar_t *buf, size_t size, size_t count, const wchar_t *fmt,
                     void *locale, va_list ap) {
    (void)count;   // the caller's own buffer size travels in `size` here, as the image's copy passes both
    return libc_vswprintf(opts, buf, size, fmt, locale, ap);
}

// ---- sorting / searching / time / process ----
void libc_qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) { qsort(base, n, size, cmp); }
void *libc_bsearch(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
    return bsearch(key, base, n, size, cmp);
}
long long libc_time64(long long *t) {
    long long v = (long long)time(0);
    if (t) *t = v;
    return v;
}
void libc_abort() { abort(); }
// cJSON's atexit wrapper registers through this on Windows. glibc does not put atexit() itself in the shared
// C library (it is in libc_nonshared.a, which a shared-object link does not take), and what that atexit does is
// call __cxa_atexit with this image's handle - so the registration goes to that entry point here, and the
// handle is any address that lies inside this object. It has been in libc.so since GLIBC_2.2.5.
typedef void (*onexit_t)(void);
extern "C" int __cxa_atexit(void (*fn)(void *), void *arg, void *dso);
int libc_register_onexit_function(void *table, onexit_t fn) {
    (void)table;
    static char this_image;
    return __cxa_atexit((void (*)(void *))fn, 0, &this_image) == 0 ? 0 : -1;
}
// an import the image only reaches from the unused crt_initialized; its target is not identifiable (returns "no")
unsigned char crt_stub_returns_zero() { return 0; }

// ---- math ----
double libc_acos(double v) { return acos(v); }
double libc_atan2(double y, double x) { return atan2(y, x); }
float libc_hypotf(float a, float b) { return hypotf(a, b); }
double libc_hypot(double x, double y) { return hypot(x, y); }
float libc_powf(float x, float y) { return powf(x, y); }
float libc_hypotf_alt(float x, float y) { return hypotf(x, y); }
float libc_truncf(float v) { return truncf(v); }
double libc_sqrt(double x) { return sqrt(x); }
float libc_sqrtf(float x) { return sqrtf(x); }
double libc_sin(double x) { return sin(x); }
float libc_sinf(float v) { return sinf(v); }
float libc_roundf(float v) { return roundf(v); }
float libc_logf(float x) { return logf(x); }
double libc_fmod(double x, double y) { return fmod(x, y); }
float libc_fmodf(float x, float y) { return fmodf(x, y); }
float libc_ceilf(float v) { return ceilf(v); }
double libc_floor(double x) { return floor(x); }
float libc_floorf(float x) { return floorf(x); }
float libc_cosf(float v) { return cosf(v); }
}
