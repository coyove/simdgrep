#ifndef _PRINTER_H
#define _PRINTER_H

#include "grepper.h"

#include <pthread.h>

#define LOCK() pthread_mutex_lock(&flags.lock)
#define UNLOCK() pthread_mutex_unlock(&flags.lock)

#define DBG(msg, ...) if (flags.quiet < -1) { LOCK(); printf(msg, ##__VA_ARGS__); UNLOCK(); }
#define LOG(msg, ...) if (flags.quiet < 0) { LOCK(); printf(msg, ##__VA_ARGS__); UNLOCK(); }
#define WARN(msg, ...) if (flags.quiet <= 0) { LOCK(); fprintf(stderr, msg, ##__VA_ARGS__); UNLOCK(); }
#define ERR0(msg, ...) if (flags.quiet <= 1) { LOCK(); fprintf(stderr, msg "\n", ##__VA_ARGS__); UNLOCK(); }
#define ERR(msg, ...) if (flags.quiet <= 1) { LOCK(); fprintf(stderr, msg ": %s\n", ##__VA_ARGS__, strerror(errno)); UNLOCK(); }

struct _flags {
    char cwd[PATH_MAX];
    bool color;
    bool fixed_string;
    bool no_ignore;
    bool no_symlink;
    int num_threads;
    int quiet;
    int xbytes;
    int verbose;
    pthread_mutex_t lock; 
    int64_t _Atomic ignores;
    int64_t _Atomic files;
};

extern struct _flags flags;

bool print_callback(const struct grepline *l);

#endif
