// The Win32 subset the engine calls (include/slinky/win32.h), served by POSIX.
//
// Why this file exists instead of a per-call #ifdef in src/engine/protected/files.cpp: those functions are the
// client's recovered code, and their Windows behaviour - the wide paths with backslash separators, FILETIME as
// 100 ns ticks since 1601, ERROR_ALREADY_EXISTS after a mkdir that lost the race, ERROR_NO_MORE_FILES to end a
// directory walk, the last-error word read right after the call that set it - is what the code around them was
// written against. Reproducing that contract here keeps the recovered logic 1:1 on both platforms, which is the
// part that has to stay provably the same.
//
// wchar_t is 2 bytes here (-fshort-wchar), as the image's UTF-16 paths require, so no libc wide function is
// callable from this build; every conversion below is done by hand on 2-byte units.
#define _GNU_SOURCE 1
#include "slinky/win32.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// the handful of Win32 codes the header does not carry
#define ERROR_SUCCESS 0u
#define ERROR_GEN_FAILURE 31u
#define ERROR_SHARING_VIOLATION 32u
#define ERROR_PATH_NOT_FOUND 3u
#define ERROR_ACCESS_DENIED 5u
#define ERROR_INVALID_HANDLE 6u
#define ERROR_NOT_ENOUGH_MEMORY 8u
#define ERROR_INVALID_PARAMETER 87u
#define ERROR_BAD_PATHNAME 161u
#define ERROR_DIRECTORY 267u
#define ERROR_MOD_NOT_FOUND 126u
#define ERROR_INSUFFICIENT_BUFFER 122u
#define OPEN_ALWAYS 4u
#define FILE_FLAG_BACKUP_SEMANTICS 0x200u
#define FILE_APPEND_DATA 0x4u
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define SE_ERR_FORK 26
#define SE_ERR_INVALIDARG 10

// ---- last-error word (Win32 codes, not errno) ----
static __thread DWORD last_error_word;
static void set_last_error(DWORD code) { last_error_word = code; }
extern "C" DWORD GetLastError() { return last_error_word; }

static DWORD errno_to_win32(int e) {
    switch (e) {
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENAMETOOLONG: return ERROR_PATH_NOT_FOUND;   // what Windows reports for a path past the limit
    case EACCES: case EPERM: case EISDIR: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_FILE_EXISTS;
    case ENOTDIR: return ERROR_DIRECTORY;
    case ENFILE: case EMFILE: case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
    case EINVAL: return ERROR_INVALID_PARAMETER;
    default: return ERROR_GEN_FAILURE;
    }
}

// ---- the two stat calls, at the kernel entry points every glibc has ----
// fstat() and fstatat() only became real C library entry points at glibc 2.33; on an older one the same work
// sat behind __fxstat(), and a binary naming the new symbols simply fails to load there. struct stat on
// x86-64 is the kernel's layout either way, so going to syscall() directly keeps one file for every
// distribution in the support list, and glibc's syscall() still sets errno for the error translation above.
static int stat_fd(int fd, struct stat *st) { return (int)syscall(SYS_fstat, fd, st); }
static int stat_at(int dir, const char *name, struct stat *st) {
    return (int)syscall(SYS_newfstatat, dir, name, st, AT_SYMLINK_NOFOLLOW);
}

// ---- UTF-16 (2-byte) <-> UTF-8 ----
// Both directions measure when the destination is absent, which is how the caller can ask "how wide is this".
// A path is folded '\' -> '/' on the way out: that is what makes the image's backslash joining resolve on POSIX.
#define PATH_BYTES 8192
#define PATH_UNITS (PATH_BYTES / 2)

static unsigned long long wide_to_narrow(const wchar_t *src, char *dst, unsigned long long dst_size) {
    unsigned long long o = 0;
    for (unsigned long long i = 0; src[i];) {
        unsigned c = (unsigned)(unsigned short)src[i];
        unsigned cp = c;
        if (c >= 0xD800 && c <= 0xDBFF && src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((c - 0xD800) << 10) + (unsigned)(src[i + 1] - 0xDC00);
            i += 2;
        } else {
            i++;
        }
        unsigned n;
        unsigned char b[4];
        if (cp < 0x80) { b[0] = (unsigned char)cp; n = 1; }
        else if (cp < 0x800) { b[0] = (unsigned char)(0xC0 | cp >> 6); b[1] = (unsigned char)(0x80 | (cp & 0x3f)); n = 2; }
        else if (cp < 0x10000) { b[0] = (unsigned char)(0xE0 | cp >> 12); b[1] = (unsigned char)(0x80 | (cp >> 6 & 0x3f));
                                 b[2] = (unsigned char)(0x80 | (cp & 0x3f)); n = 3; }
        else { b[0] = (unsigned char)(0xF0 | cp >> 18); b[1] = (unsigned char)(0x80 | (cp >> 12 & 0x3f));
               b[2] = (unsigned char)(0x80 | (cp >> 6 & 0x3f)); b[3] = (unsigned char)(0x80 | (cp & 0x3f)); n = 4; }
        if (c == L'\\') b[0] = '/';   // a lone backslash never starts a UTF-8 sequence, so the fold is safe on byte 0
        if (!dst) { o += n; continue; }
        if (o + n >= dst_size) return (unsigned long long)-1;   // no room: fail instead of handing back a short path
        for (unsigned k = 0; k < n; k++) dst[o++] = (char)b[k];
    }
    if (dst) {
        if (o >= dst_size) return (unsigned long long)-1;
        dst[o] = 0;
    }
    return o;
}

static unsigned long long narrow_to_wide(const char *src, wchar_t *dst, unsigned long long dst_units) {
    unsigned long long o = 0;
    for (unsigned long long i = 0; src[i];) {
        unsigned char c = (unsigned char)src[i];
        unsigned cp, extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1fu; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0fu; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; extra = 3; }
        else { i++; continue; }   // invalid lead byte: dropped, as the converter does for unpaired input
        // Only a multi-byte sequence can be truncated: a one-byte character is complete on its own, and
        // testing src[i+1] for it used to drop the last character of every path - $HOME became /home/us.
        if (extra && src[i + 1] == 0) break;
        i++;
        for (unsigned k = 0; k < extra; k++) {
            if (((unsigned char)src[i] & 0xC0) != 0x80) { cp = 0; break; }
            cp = cp << 6 | ((unsigned char)src[i++] & 0x3f);
        }
        if (!cp) continue;
        if (cp > 0xFFFF) {
            if (dst) {
                if (o + 2 >= dst_units) return (unsigned long long)-1;
                cp -= 0x10000;
                dst[o] = (wchar_t)(0xD800 + (cp >> 10));
                dst[o + 1] = (wchar_t)(0xDC00 + (cp & 0x3ff));
            }
            o += 2;
        } else {
            if (dst) {
                if (o + 1 >= dst_units) return (unsigned long long)-1;
                dst[o] = (wchar_t)cp;
            }
            o++;
        }
    }
    if (dst) {
        if (o >= dst_units) return (unsigned long long)-1;
        dst[o] = 0;
    }
    return o;
}

// the caller's path, converted with the separator fold; 0 when it does not fit
static char *path_to_narrow(const wchar_t *path, char *buf, unsigned long long size) {
    if (!path) return 0;
    if (wide_to_narrow(path, buf, size) == (unsigned long long)-1) {
        set_last_error(ERROR_BAD_PATHNAME);
        return 0;
    }
    return buf;
}

extern "C" int MultiByteToWideChar(UINT codepage, DWORD flags, const char *src, int n, wchar_t *dst, int dst_size) {
    (void)flags;
    if (!src || dst_size < 0) return 0;
    if (codepage != 65001 /* CP_UTF8 */ && codepage != 1252 && codepage != 0) return 0;
    unsigned long long units;
    if (n < 0) {
        units = narrow_to_wide(src, dst, dst_size ? (unsigned long long)dst_size : 0);
    } else {
        char *tmp = (char *)malloc((size_t)n + 1);   // an explicit length is not NUL-terminated
        if (!tmp) return 0;
        memcpy(tmp, src, (size_t)n);
        tmp[n] = 0;
        units = narrow_to_wide(tmp, dst, dst_size ? (unsigned long long)dst_size : 0);
        free(tmp);
    }
    if (units == (unsigned long long)-1) { set_last_error(ERROR_INSUFFICIENT_BUFFER); return 0; }
    if (!dst_size) return (int)units + (n < 0 ? 1 : 0);   // the "how many units would you need" query
    return (int)units;
}

extern "C" int WideCharToMultiByte(UINT codepage, DWORD flags, const wchar_t *src, int n, char *dst, int dst_size,
                                   const char *def, BOOL *used_def) {
    (void)flags; (void)def;
    if (used_def) *used_def = 0;
    if (!src || dst_size < 0) return 0;
    if (codepage != 65001 && codepage != 1252 && codepage != 0) return 0;
    unsigned long long bytes;
    if (n < 0) {
        bytes = wide_to_narrow(src, dst, dst_size ? (unsigned long long)dst_size : 0);
    } else {
        wchar_t *tmp = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
        if (!tmp) return 0;
        for (int i = 0; i < n; i++) tmp[i] = src[i];
        tmp[n] = 0;
        bytes = wide_to_narrow(tmp, dst, dst_size ? (unsigned long long)dst_size : 0);
        free(tmp);
    }
    if (bytes == (unsigned long long)-1) { set_last_error(ERROR_INSUFFICIENT_BUFFER); return 0; }
    if (!dst_size) return (int)bytes + (n < 0 ? 1 : 0);
    return (int)bytes;
}

// ---- clock ----
extern "C" BOOL QueryPerformanceCounter(long long *count) {
    struct timespec ts;
    if (!count || clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    *count = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return 1;
}
extern "C" BOOL QueryPerformanceFrequency(long long *freq) {
    if (!freq) return 0;
    *freq = 1000000000LL;   // the counter above counts nanoseconds
    return 1;
}
extern "C" void GetSystemTimeAsFileTime(FILETIME *ft) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    // 100 ns ticks since 1601-01-01, which is 11644473600 seconds ahead of the POSIX epoch
    unsigned long long t = (unsigned long long)ts.tv_sec * 10000000ULL + (unsigned long long)(ts.tv_nsec / 100)
                         + 116444736000000000ULL;
    ft->lo = (DWORD)t;
    ft->hi = (DWORD)(t >> 32);
}

// ---- handles: everything the engine hands to CloseHandle shares one tagged record ----
enum HandleKind { H_FILE, H_EVENT, H_FIND, H_THREAD, H_STD };
struct Handle {
    HandleKind kind;
    int fd;
    DIR *dir;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int signaled, manual_reset;
};
static Handle *handle_new(HandleKind kind) {
    Handle *h = (Handle *)calloc(1, sizeof(Handle));
    if (h) h->kind = kind;
    return h;
}

extern "C" BOOL CloseHandle(HANDLE h) {
    Handle *o = (Handle *)h;
    if (!o) { set_last_error(ERROR_INVALID_HANDLE); return 0; }
    switch (o->kind) {
    case H_FILE: close(o->fd); break;
    case H_FIND: if (o->dir) closedir(o->dir); break;
    case H_EVENT: pthread_mutex_destroy(&o->mutex); pthread_cond_destroy(&o->cond); break;
    case H_STD:   // the process's own streams: the record goes, the descriptor stays
    case H_THREAD: break;
    }
    free(o);
    return 1;
}

// ---- files ----
extern "C" HANDLE CreateFileW(const wchar_t *path, DWORD access, DWORD share, void *security, DWORD disposition,
                              DWORD flags, HANDLE templ) {
    (void)share; (void)security; (void)templ;
    (void)flags;   // attributes are a Windows concept; only the backup-semantics hint would matter, and the
                   // directory walk below uses opendir() instead of CreateFileW
    char name[PATH_BYTES];
    if (!path_to_narrow(path, name, sizeof name)) return INVALID_HANDLE;
    int oflag;
    if ((access & (GENERIC_READ | GENERIC_WRITE)) == (GENERIC_READ | GENERIC_WRITE)) oflag = O_RDWR;
    else if (access & GENERIC_WRITE) oflag = O_WRONLY;
    else if (access & FILE_APPEND_DATA) oflag = O_WRONLY | O_APPEND;
    else oflag = O_RDONLY;
    switch (disposition) {
    case CREATE_NEW: oflag |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: oflag |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: oflag |= O_CREAT; break;
    case OPEN_EXISTING: break;
    default: set_last_error(ERROR_INVALID_PARAMETER); return INVALID_HANDLE;
    }
    int fd;
    for (;;) {
        fd = open(name, oflag, 0600);
        if (fd >= 0) break;
        if (errno == EINTR) continue;
        if (disposition == CREATE_NEW && errno == EEXIST) set_last_error(ERROR_FILE_EXISTS);
        else set_last_error(errno_to_win32(errno));
        return INVALID_HANDLE;
    }
    Handle *h = handle_new(H_FILE);
    if (!h) { close(fd); set_last_error(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE; }
    h->fd = fd;
    set_last_error(ERROR_SUCCESS);
    return (HANDLE)h;
}

extern "C" DWORD GetFileSize(HANDLE h, DWORD *high) {
    Handle *o = (Handle *)h;
    if (!o || (o->kind != H_FILE && o->kind != H_STD)) { set_last_error(ERROR_INVALID_HANDLE); return INVALID_FILE_SIZE; }
    struct stat st;
    if (stat_fd(o->fd, &st)) { set_last_error(errno_to_win32(errno)); return INVALID_FILE_SIZE; }
    if (high) *high = (DWORD)((unsigned long long)st.st_size >> 32);
    if ((unsigned long long)st.st_size > 0xffffffffull) { set_last_error(ERROR_INVALID_PARAMETER); return INVALID_FILE_SIZE; }
    return (DWORD)st.st_size;
}

extern "C" BOOL ReadFile(HANDLE h, void *buf, DWORD n, DWORD *done, void *overlapped) {
    (void)overlapped;
    Handle *o = (Handle *)h;
    if (!o || (o->kind != H_FILE && o->kind != H_STD)) { set_last_error(ERROR_INVALID_HANDLE); return 0; }
    unsigned long long got = 0;
    while (got < n) {
        ssize_t r = read(o->fd, (char *)buf + got, (size_t)(n - got));
        if (r < 0) {
            if (errno == EINTR) continue;
            set_last_error(errno_to_win32(errno));
            if (done) *done = (DWORD)got;
            return 0;
        }
        if (!r) break;   // end of file: a short read, which the caller turns into its own error
        got += (unsigned long long)r;
    }
    if (done) *done = (DWORD)got;
    set_last_error(ERROR_SUCCESS);
    return 1;
}

extern "C" BOOL WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *done, void *overlapped) {
    (void)overlapped;
    Handle *o = (Handle *)h;
    if (!o || (o->kind != H_FILE && o->kind != H_STD)) { set_last_error(ERROR_INVALID_HANDLE); return 0; }
    unsigned long long put = 0;
    while (put < n) {
        ssize_t r = write(o->fd, (const char *)buf + put, (size_t)(n - put));
        if (r < 0) {
            if (errno == EINTR) continue;
            set_last_error(errno_to_win32(errno));
            if (done) *done = (DWORD)put;
            return 0;
        }
        if (!r) { set_last_error(ERROR_SHARING_VIOLATION); if (done) *done = (DWORD)put; return 0; }
        put += (unsigned long long)r;
    }
    if (done) *done = (DWORD)put;
    set_last_error(ERROR_SUCCESS);
    return 1;
}

extern "C" BOOL CreateDirectoryW(const wchar_t *path, void *security) {
    (void)security;
    char name[PATH_BYTES];
    if (!path_to_narrow(path, name, sizeof name)) return 0;
    if (mkdir(name, 0777) == 0) { set_last_error(ERROR_SUCCESS); return 1; }
    // the caller reads the error word to tell "someone else won the race" from a real failure
    set_last_error(errno == EEXIST ? ERROR_ALREADY_EXISTS : errno_to_win32(errno));
    return 0;
}

extern "C" BOOL DeleteFileW(const wchar_t *path) {
    char name[PATH_BYTES];
    if (!path_to_narrow(path, name, sizeof name)) return 0;
    if (unlink(name) == 0) { set_last_error(ERROR_SUCCESS); return 1; }
    if (errno == EISDIR) set_last_error(ERROR_ACCESS_DENIED);   // Windows refuses to delete a directory the same way
    else set_last_error(errno_to_win32(errno));
    return 0;
}

// ---- directory listing: the pattern the recovered find_files builds is always "<dir>\<*>" ----
static void fill_find_data(Handle *o, const struct dirent *re, WIN32_FIND_DATAW *fd) {
    memset(fd, 0, sizeof *fd);
    narrow_to_wide(re->d_name, fd->name, 260);
    struct stat st;
    int is_dir = re->d_type == DT_DIR;
    if (stat_at(dirfd(o->dir), re->d_name, &st) == 0) {
        is_dir = S_ISDIR(st.st_mode);
        unsigned long long t = (unsigned long long)st.st_mtim.tv_sec * 10000000ULL
                             + (unsigned long long)(st.st_mtim.tv_nsec / 100) + 116444736000000000ULL;
        fd->written.lo = (DWORD)t;
        fd->written.hi = (DWORD)(t >> 32);
        fd->created = fd->accessed = fd->written;
        fd->size_lo = (DWORD)st.st_size;
        fd->size_hi = (DWORD)((unsigned long long)st.st_size >> 32);
    }
    fd->attributes = (DWORD)(is_dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL);
}

extern "C" HANDLE FindFirstFileW(const wchar_t *pattern, WIN32_FIND_DATAW *fd) {
    char path[PATH_BYTES];
    if (!pattern || !fd || !path_to_narrow(pattern, path, sizeof path)) return INVALID_HANDLE;
    char *dir_name = path;
    char *slash = strrchr(path, '/');
    if (slash) {
        *slash = 0;
        if (!path[0]) dir_name = (char *)"/";   // the pattern was "/*"
    } else {
        dir_name = (char *)".";   // a bare "*"
    }
    DIR *dir = opendir(dir_name);
    if (!dir) {
        set_last_error(errno == ENOENT || errno == ENOTDIR ? ERROR_FILE_NOT_FOUND : errno_to_win32(errno));
        return INVALID_HANDLE;
    }
    Handle *h = handle_new(H_FIND);
    if (!h) { closedir(dir); set_last_error(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE; }
    h->dir = dir;
    if (!FindNextFileW((HANDLE)h, fd)) {   // the first entry, with the walk's own end-of-directory error left set
        DWORD e = last_error_word;
        closedir(dir);
        free(h);
        // An empty directory is "nothing to find", not a read failure. Windows reports ERROR_FILE_NOT_FOUND
        // here; passing the walk's end-of-directory code through made the profile scan see an error on every
        // fresh Linux install, where the profiles folder exists and has no files in it yet.
        set_last_error(e == ERROR_NO_MORE_FILES ? ERROR_FILE_NOT_FOUND : e);
        return INVALID_HANDLE;
    }
    return (HANDLE)h;
}

extern "C" BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW *fd) {
    Handle *o = (Handle *)h;
    if (!o || o->kind != H_FIND || !fd) { set_last_error(ERROR_INVALID_HANDLE); return 0; }
    struct dirent *re;
    for (;;) {
        errno = 0;
        re = readdir(o->dir);
        if (!re) { set_last_error(errno ? ERROR_GEN_FAILURE : ERROR_NO_MORE_FILES); return 0; }
        if (strcmp(re->d_name, ".") && strcmp(re->d_name, "..")) break;
    }
    fill_find_data(o, re, fd);
    set_last_error(ERROR_SUCCESS);
    return 1;
}

extern "C" BOOL FindClose(HANDLE h) { return CloseHandle(h); }

// ---- process, threads, events ----
extern "C" DWORD GetCurrentProcessId() { return (DWORD)getpid(); }

extern "C" void Sleep(DWORD ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

struct ThreadStart {
    THREAD_START fn;
    void *param;
};
static void *thread_trampoline(void *arg) {
    struct ThreadStart s = *(struct ThreadStart *)arg;
    free(arg);
    s.fn(s.param);
    return 0;
}

extern "C" HANDLE CreateThread(void *security, size_t stack, THREAD_START start, LPVOID param, DWORD flags,
                               DWORD *id) {
    (void)security; (void)flags;
    struct ThreadStart *s = (struct ThreadStart *)malloc(sizeof *s);
    Handle *h = handle_new(H_THREAD);
    if (!s || !h) { free(s); free(h); set_last_error(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    s->fn = start;
    s->param = param;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stack >= (size_t)PTHREAD_STACK_MIN) pthread_attr_setstacksize(&attr, stack);
    // detached: the engine thread outlives the handle that started it, and nothing in this code joins
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    int r = pthread_create(&t, &attr, thread_trampoline, s);
    pthread_attr_destroy(&attr);
    if (r) { free(s); free(h); set_last_error(errno_to_win32(r)); return 0; }
    if (id) *id = 0;
    set_last_error(ERROR_SUCCESS);
    return (HANDLE)h;
}

extern "C" HANDLE CreateEventW(void *security, BOOL manual_reset, BOOL initial_state, const wchar_t *name) {
    (void)security;
    (void)name;   // named events are only the Windows "is an engine already in this process" marker; on Linux
                  // that is a file lock, done in src/platform/linux/entry.cpp
    Handle *h = handle_new(H_EVENT);
    if (!h) { set_last_error(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    pthread_mutex_init(&h->mutex, 0);
    // CLOCK_MONOTONIC, because WaitForSingleObject builds its deadline from it. With the default
    // CLOCK_REALTIME the deadline (seconds since boot) is always in the past, so every bounded wait
    // returned WAIT_TIMEOUT instantly and nothing that waits - the unload handshake above all - finished.
    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
    pthread_cond_init(&h->cond, &cattr);
    pthread_condattr_destroy(&cattr);
    h->manual_reset = manual_reset;
    h->signaled = initial_state;
    set_last_error(ERROR_SUCCESS);
    return (HANDLE)h;
}

extern "C" BOOL SetEvent(HANDLE e) {
    Handle *h = (Handle *)e;
    if (!h || h->kind != H_EVENT) { set_last_error(ERROR_INVALID_HANDLE); return 0; }
    pthread_mutex_lock(&h->mutex);
    h->signaled = 1;
    if (h->manual_reset) pthread_cond_broadcast(&h->cond);
    else pthread_cond_signal(&h->cond);
    pthread_mutex_unlock(&h->mutex);
    return 1;
}

extern "C" DWORD WaitForSingleObject(HANDLE h, DWORD ms) {
    Handle *o = (Handle *)h;
    if (!o) return WAIT_FAILED;
    if (o->kind == H_THREAD) return WAIT_OBJECT_0;   // detached: there is nothing to wait for, and nobody does
    if (o->kind != H_EVENT) { set_last_error(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
    struct timespec deadline;
    int bounded = ms != INFINITE;
    if (bounded) {
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec += (time_t)(ms / 1000);
        deadline.tv_nsec += (long)(ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) { deadline.tv_nsec -= 1000000000L; deadline.tv_sec++; }
    }
    pthread_mutex_lock(&o->mutex);
    int timed_out = 0;
    while (!o->signaled) {
        if (!bounded) {
            pthread_cond_wait(&o->cond, &o->mutex);
            continue;
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
            timed_out = 1;
            break;
        }
        if (pthread_cond_timedwait(&o->cond, &o->mutex, &deadline) == ETIMEDOUT) {
            if (!o->signaled) timed_out = 1;   // a wake and a timeout can race; the state decides
            break;
        }
    }
    if (!timed_out && !o->manual_reset) o->signaled = 0;   // an auto-reset event is consumed by the wake
    pthread_mutex_unlock(&o->mutex);
    return timed_out ? WAIT_TIMEOUT : WAIT_OBJECT_0;
}

// ---- module and symbol lookups (kernel32_lookups / get_proc_address in the recovered code) ----
// GLFW is the real user: LWJGL 3 clients have libglfw already mapped in this process, so the no-load spelling
// finds it and a miss means exactly what it means on Windows - this client is LWJGL 2, which init treats as
// "not an error".
// glibc's no-load lookup matches the exact path an object was mapped from, but the recovered code asks the Win32
// way - a base name with no path and no suffix ("jvm", "glfw"). /proc/self/maps turns that name back into the
// file this process really mapped, which is how the Java VM and GLFW get found in a running client.
static void *find_mapped_module(const char *name) {
    char needle[300];
    if ((size_t)snprintf(needle, sizeof needle, "lib%s.", name) >= sizeof needle) return 0;
    size_t needle_len = strlen(needle);
    FILE *maps = fopen("/proc/self/maps", "re");
    if (!maps) return 0;
    void *handle = 0;
    char line[1024];
    while (fgets(line, sizeof line, maps)) {
        // The name is the sixth field, not the text after the last space: a mapped path may contain spaces
        // ("/home/me/My Games/..."), and only tab and newline are escaped there.
        char *path = line;
        for (int f = 0; f < 5; f++) {
            while (*path == ' ') path++;
            if (!*path) break;
            while (*path && *path != ' ') path++;
        }
        while (*path == ' ') path++;
        if (!*path || *path == '[' || *path == '\n') continue;
        char *nl = strpbrk(path, "\r\n");   // fgets keeps the line ending, and dlopen() of "libjvm.so\n" finds
        if (nl) *nl = 0;                    // nothing - this lookup silently failed for every mapped file
        char *slash = strrchr(path, '/');
        const char *base = slash ? slash + 1 : path;
        // "libjvm.so" for "jvm", "libglfw.so.3" for "glfw", and the plain name if that is how it is spelled
        if (strncmp(base, needle, needle_len) != 0 && strcmp(base, name) != 0) continue;
        handle = dlopen(path, RTLD_LAZY | RTLD_NOLOAD);
        if (handle) break;
    }
    fclose(maps);
    return handle;
}

extern "C" HMODULE GetModuleHandleA(const char *name) {
    if (!name) return (HMODULE)RTLD_DEFAULT;
    const char *prefixes[] = {"", "lib"};
    const char *suffixes[] = {"", ".so", ".so.1", ".so.2", ".so.3"};
    for (const char *pfx : prefixes) {
        for (const char *sfx : suffixes) {
            char full[512];
            if ((size_t)snprintf(full, sizeof full, "%s%s%s", pfx, name, sfx) >= sizeof full) continue;
            void *h = dlopen(full, RTLD_LAZY | RTLD_NOLOAD);
            if (h) return (HMODULE)h;
        }
    }
    void *mapped = find_mapped_module(name);
    if (mapped) return (HMODULE)mapped;
    set_last_error(ERROR_MOD_NOT_FOUND);
    return 0;
}

extern "C" HMODULE LoadLibraryA(const char *name) {
    if (!name) return 0;
    void *h = dlopen(name, RTLD_LAZY | RTLD_GLOBAL);
    if (!h) {
        char full[512];
        if ((size_t)snprintf(full, sizeof full, "lib%s.so", name) < sizeof full)
            h = dlopen(full, RTLD_LAZY | RTLD_GLOBAL);
    }
    if (!h) { set_last_error(ERROR_MOD_NOT_FOUND); return 0; }
    return (HMODULE)h;
}

extern "C" void *GetProcAddress(HMODULE m, const char *name) {
    if (!name) return 0;
    return dlsym(m ? (void *)m : RTLD_DEFAULT, name);
}

// the recovered code's two names for those lookups (on Windows they live in engine/protected/input.cpp)
extern "C" HMODULE kernel32_lookups(const char *module) { return GetModuleHandleA(module); }
extern "C" void *get_proc_address(HMODULE m, const char *name) { return GetProcAddress(m, name); }

// ---- the process's own streams, for the log writer ----
extern "C" HANDLE GetStdHandle(DWORD which) {
    Handle *h = handle_new(H_STD);
    if (!h) { set_last_error(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE; }
    h->fd = which == STD_OUTPUT_HANDLE ? 1 : 2;
    return (HANDLE)h;
}

extern "C" void OutputDebugStringA(const char *text) {
    (void)text;   // no debugger channel here; slinky_log_write already reaches stderr and the log file
}

extern "C" BOOL DisableThreadLibraryCalls(HMODULE m) { (void)m; return 1; }

// ---- "show me this folder" (the settings menu's open-folder button) ----
// The opener must not outlive the call as a zombie of the game process, so it is double-forked and only the
// intermediate child is reaped here.
extern "C" HINSTANCE ShellExecuteW(HWND hwnd, const wchar_t *op, const wchar_t *file, const wchar_t *params,
                                   const wchar_t *dir, int show) {
    (void)hwnd; (void)op; (void)params; (void)dir; (void)show;
    char path[PATH_BYTES];
    if (!path_to_narrow(file, path, sizeof path)) return (HINSTANCE)(LONG_PTR)SE_ERR_INVALIDARG;
    pid_t first = fork();
    if (first < 0) return (HINSTANCE)(LONG_PTR)SE_ERR_FORK;
    if (first == 0) {
        pid_t second = fork();
        if (second == 0) {
            setsid();
            char *argv[] = {(char *)"xdg-open", path, 0};
            execvp("xdg-open", argv);
            _exit(127);
        }
        _exit(second < 0 ? 1 : 0);
    }
    int status;
    while (waitpid(first, &status, 0) < 0 && errno == EINTR) { }
    return (HINSTANCE)(LONG_PTR)42;   // anything above 32 means "started" to the caller
}

// ---- the environment block ----
// The recovered code asks for %USERPROFILE% to find the profile folder; on POSIX that is $HOME. The name it
// passes is UTF-16, so the lookup goes through the same conversion as the paths.
extern "C" wchar_t *_wgetenv(const wchar_t *name) {
    static __thread wchar_t value[PATH_UNITS];
    char narrow[256];
    if (!name || wide_to_narrow(name, narrow, sizeof narrow) == (unsigned long long)-1) return 0;
    if (!narrow[0]) return 0;
    const char *v = getenv(!strcmp(narrow, "USERPROFILE") ? "HOME" : narrow);
    if ((!v || !v[0]) && !strcmp(narrow, "USERPROFILE")) {
        // A process started without an environment - a service-style launcher, an ssh session with a stripped
        // env, a sandbox that passes nothing through - has no $HOME, and the passwd entry is where the home
        // folder actually is. Without this the client simply stops finding its profiles.
        struct passwd *pw = getpwuid(getuid());
        if (pw && pw->pw_dir && pw->pw_dir[0]) v = pw->pw_dir;
    }
    if (!v || !v[0]) return 0;
    if (narrow_to_wide(v, value, PATH_UNITS) == (unsigned long long)-1) return 0;
    return value;
}
