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

#ifdef __MAX_BRUTE_FORCE_LENGTH
static const int MAX_BRUTE_FORCE_LENGTH = __MAX_BRUTE_FORCE_LENGTH;
#else
static const int MAX_BRUTE_FORCE_LENGTH = 0;
#endif

#define BINARY 0
#define BINARY_TEXT 1
#define BINARY_IGNORE 2

#define OPEN_BINARY_SKIPPED -100
#define FILL_OK -1
#define FILL_EMPTY -2
#define FILL_LAST_CHUNK -8
#define FILL_EOF -9

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

struct grefile;

struct grepline {
    struct grepper *g;
    struct grepfile *file;
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
    int len;
    int before_lines;
    int after_lines;
    uint64_t _Atomic falses;
    bool (*callback)(const struct grepline *);

    int _table[256];
    uint8_t _case_mask8;
    uint16_t _case_mask16;
    uint32_t _case_mask32;
    struct rx_pattern_info rx;
    struct grepper *next_g;
};

struct grepfile {
    int fd;
    const char *name;
    int64_t size;
    int64_t lines;
    int64_t off;
    bool binary_matching;
    bool is_binary;
    uint32_t _Atomic lock;
    char *tail;
    ssize_t tail_size;
};

struct grepfile_chunk {
    int64_t prev_lines;
    char *buf;
    ssize_t buf_size;
    ssize_t data_size;
};

struct worker {
    pthread_t thread;
    int32_t _Atomic *actives;
    struct grepfile_chunk chunk;
};

int torune(uint32_t *rune, const char *s);

const char *unsafecasestr(const char *s);

void grepper_init(struct grepper *g, const char *find, bool ignore_case);

void grepper_init_rx(struct grepper *g, const char *s, bool ignore_case);

struct grepper *grepper_add(struct grepper *g, const char *find);

void grepper_free(struct grepper *g);

int64_t grepper_find(struct grepper *g, const char *s, int64_t ls);

bool grepper_match(struct grepper *g, struct grepline *gl, csview *rx_match,
        const char *line_start, const char *s, const char *line_end);

int grepfile_open(const char *, struct grepper *, struct grepfile *, struct grepfile_chunk *);

int grepfile_release(struct grepfile *file);

int grepfile_process(struct grepper *, struct grepfile *, struct grepfile_chunk *);

int64_t countbyte(const char *s, const char *end, uint8_t c);

const char *indexbyte(const char *s, const char *end, const uint8_t a);

const char *indexlastbyte(const char *start, const char *s, const uint8_t a);

int buffer_fill(struct grepfile *, struct grepfile_chunk *);

#endif
