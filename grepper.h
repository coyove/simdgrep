#ifndef _GREPPER_H
#define _GREPPER_H
 
#include <regex.h>
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
#include <regex.h>

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

static const int BINARY = 0;
static const int BINARY_TEXT = 1;
static const int BINARY_IGNORE = 2;

struct grepper_ctx;

struct rx_pattern_info {
    bool use_regex;
    regex_t engine;
    bool fixed_start;
    char *error;
    const char *unsupported_escape;
};

struct grepline {
    struct grepper_ctx *ctx;
    struct grepper *g;
    int64_t nr;
    bool is_afterline;
    const char *line;
    int len;
    int match_start;
    int match_end;
};

struct grepper {
    char *find;
    char *findupper;
    char *findlower;
    bool ignore_case;
    int binary_mode;
    int len;
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

void grepper_init(struct grepper *g, const char *find, bool ignore_case);

struct grepper *grepper_add(struct grepper *g, const char *find);

void grepper_free(struct grepper *g);

int64_t grepper_find(struct grepper *g, const char *s, int64_t ls);

int grepper_file(struct grepper *, const char *, struct grepper_ctx *);

int64_t countbyte(const char *s, const char *end, uint8_t c);

const char *indexbyte(const char *s, const char *end, const uint8_t a);

const char *indexlastbyte(const char *start, const char *s, const uint8_t a);

void grepper_init_rx(struct grepper *g, const char *s, bool ignore_case);

// line buffer
//
struct linebuf {
    int fd;
    int64_t lines;
    int len;
    int datalen;
    int buflen;
    bool overflowed;
    bool ignore_counting_lines;
    bool is_binary;
    char *buffer;
};

struct grepper_ctx {
    void *memo;
    struct linebuf lbuf;
};

static void buffer_init(struct linebuf *l, int line_size)
{
    l->buflen = line_size;
    /* 
       Regex will use this buffer to do matching and temporarily place NULL at
       the end of line, reserve 1 byte for the last line of the file.
       */
    l->buffer = (char *)malloc(line_size + 1);
}

static void buffer_reset(struct linebuf *l, int fd)
{
    l->fd = fd;
    l->overflowed = l->ignore_counting_lines = false;
    l->lines = l->len = l->datalen = 0;
}

static void buffer_fill(struct linebuf *l, const char *path)
{
    if (!l->ignore_counting_lines) 
        l->lines += countbyte(l->buffer, l->buffer + l->len, '\n');

    // Move tailing bytes forward.
    memcpy(l->buffer, l->buffer + l->len, l->datalen - l->len);
    l->len = l->datalen = l->datalen - l->len;

    // Fill rest space.
    int n = read(l->fd, l->buffer + l->len, l->buflen - l->len);
    if (n <= 0) {
        return;
    }

    l->len += n;
    l->datalen += n;

    const char *end = indexlastbyte(l->buffer, l->buffer + l->datalen, '\n');
    if (end) {
        l->len = end - l->buffer + 1;
    } else if (l->datalen == l->buflen) {
        l->overflowed = true;
    }
}

static void buffer_free(struct linebuf *l)
{
    free(l->buffer);
}

static int64_t now() {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    return start.tv_sec * 1000000000L + start.tv_nsec;
}

#endif
