#ifndef _GREPPER_H
#define _GREPPER_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef __APPLE__
#include <os/lock.h>
#endif

#include "stack.h"
#include "pathutil.h"

#include "STC/include/stc/cregex.h"

#if defined(__x86_64__)

#include <emmintrin.h>
#include <immintrin.h>
#include <smmintrin.h>  

#elif defined(__aarch64__)

#include <arm_neon.h>

#endif

static int64_t MAX(int64_t a, int64_t b) { return a > b ? a : b; }
static int64_t MIN(int64_t a, int64_t b) { return a < b ? a : b; }

#define BINARY 0
#define BINARY_TEXT 1
#define BINARY_IGNORE 2

#define OPEN_EMPTY -98
#define OPEN_IGNORED -99
#define OPEN_BINARY_SKIPPED -100

#define FILL_OK -1
#define FILL_LAST_CHUNK -8
#define FILL_EOF -9

#define INIT_OK 0
#define INIT_INVALID_UTF8 -2

#define STATUS_QUEUED 1
#define STATUS_OPENED 2
#define STATUS_IS_DIR 32
#define STATUS_IS_BINARY 64
#define STATUS_IS_BINARY_MATCHING 128

#define INC_NEXT 0
#define INC_FREEABLE -1
#define INC_WAIT_FREEABLE -2

static int64_t now() {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    return start.tv_sec * 1000000000L + start.tv_nsec;
}

struct rx_pattern_info {
    cregex engine;
    bool fixed_start;
    bool line_start;
    bool line_end;
    int use_regex;
    int groups;
    char *error;
};

struct grepfile;

struct grepline {
    struct grepper *g;
    struct grepfile_chunk *file;
    int64_t nr;
    bool is_ctxline;
    const char *line;
    int64_t len;
    int64_t match_start;
    int64_t match_end;
};

struct grepper {
    char *find;
    char *findupper;
    char *findlower;
    bool ignore_case;
    bool slow_rx;
    bool search_name;
    int binary_mode;
    int before_lines;
    int after_lines;
    size_t len;
    bool (*callback)(const struct grepline *);

    struct rx_pattern_info rx;
    struct grepper *next_g;
};

struct grepfile {
    struct stacknode node;
    int fd;
    char *name;
    int64_t size;
    int64_t off;
    uint16_t status;
    int32_t chunk_refs;
    int64_t lines;
    struct matcher *root_matcher;
#ifdef __APPLE__
    os_unfair_lock lock;
#else
    pthread_mutex_t lock;
#endif
};

struct grepfile_chunk {
    int64_t prev_lines;
    size_t off;
    char *buf;
    ssize_t buf_size;
    ssize_t data_size;
    struct grepfile *file;
};

struct worker {
    pthread_t thread;
    int32_t _Atomic *actives;
    uint8_t tid;
    struct grepfile_chunk chunk;
};

int torune(uint32_t *rune, const char *s);

int grepper_init(struct grepper *g, const char *find, bool ignore_case);

void grepper_init_rx(struct grepper *g, const char *s, bool ignore_case);

struct grepper *grepper_add(struct grepper *g, const char *find);

void grepper_free(struct grepper *g);

int64_t grepper_find(struct grepper *g, const char *s, int64_t ls);

bool grepper_match(struct grepper *g, struct grepline *gl, csview *rx_match,
        const char *line_start, const char *s, const char *line_end);

int grepfile_open(struct grepper *, struct grepfile *, struct grepfile_chunk *);

int grepfile_release(struct grepfile *file);

int grepfile_inc_ref(struct grepfile *file);

void grepfile_dec_ref(struct grepfile *file);

int grepfile_acquire_chunk(struct grepfile *, struct grepfile_chunk *);

void grepfile_process_chunk(struct grepper *, struct grepfile_chunk *);

int64_t countbyte(const char *s, const char *end, uint8_t c);

const char *indexbyte(const char *s, const char *end, const uint8_t a);

const char *indexlastbyte(const char *start, const char *s, const uint8_t a);

#endif
