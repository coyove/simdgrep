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

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#ifdef __MAX_BRUTE_FORCE_LENGTH
static const int MAX_BRUTE_FORCE_LENGTH = __MAX_BRUTE_FORCE_LENGTH;
#else
static const int MAX_BRUTE_FORCE_LENGTH = 16;
#endif

static const int LINE_BUF_SIZE = 65536;

static int64_t now() {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    return start.tv_sec * 1000000000L + start.tv_nsec;
}

static const int BINARY = 0;
static const int BINARY_TEXT = 1;
static const int BINARY_IGNORE = 2;

struct rx_pattern_info {
    char *fixed_patterns;
    int fixed_len;
    bool fixed_start;
    bool pure;
    const char *unsupported_escape;
};

struct grepline {
    void *memo;
    struct grepper *g;
    int64_t nr;
    bool is_binary;
    const char *line_start;
    const char *line_end;
};

struct _grepper_file {
    int after_lines;
    struct grepper *next_g;
    bool (*callback)(const struct grepline *);
};

struct grepper {
    char *find;
    char *findupper;
    char *findlower;
    int binary_mode;
    int len;
    int *table;
    uint64_t _Atomic falses;
    bool ignore_case;
    uint8_t _case_mask8;
    uint16_t _case_mask16;
    bool use_regex;
    regex_t rx;
    struct rx_pattern_info rx_info;
    struct _grepper_file file;
};

struct grepper_ctx;

void grepper_init(struct grepper *g, const char *find, bool ignore_case);

struct grepper *grepper_add(struct grepper *g, const char *find);

void grepper_free(struct grepper *g);

int64_t grepper_find(struct grepper *g, const char *s, int64_t ls);

int grepper_file(struct grepper *, const char *, int64_t, struct grepper_ctx *);

int64_t countbyte(const char *s, const char *end, uint8_t c);

const char *indexbyte(const char *s, const char *end, const uint8_t a);

const char *indexlastbyte(const char *start, const char *s, const uint8_t a);

struct rx_pattern_info rx_extract_plain(const char *s);

// line buffer
//
struct linebuf {
    int fd;
    int lines;
    int len;
    int datalen;
    char *buffer;
};

struct grepper_ctx {
    void *memo;
    struct linebuf lbuf;
};

static void buffer_init(struct linebuf *l)
{
    l->buffer = (char *)malloc(LINE_BUF_SIZE);
}

static void buffer_fill(struct linebuf *l)
{
    l->lines += countbyte(l->buffer, l->buffer + l->len, '\n');

    // Move tailing bytes forward.
    memcpy(l->buffer, l->buffer + l->len, l->datalen - l->len);
    l->len = l->datalen = l->datalen - l->len;

    // Fill rest space.
    int n = read(l->fd, l->buffer + l->len, LINE_BUF_SIZE - l->len);
    if (n <= 0) {
        return;
    }

    l->len += n;
    l->datalen += n;

    const char *end = indexlastbyte(l->buffer, l->buffer + l->datalen, '\n');
    if (end) {
        l->len = end - l->buffer;
    }
}

static void buffer_free(struct linebuf *l)
{
    free(l->buffer);
}

#endif
