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

#ifdef __MAX_BRUTE_FORCE_LENGTH
static const int MAX_BRUTE_FORCE_LENGTH = __MAX_BRUTE_FORCE_LENGTH;
#else
static const int MAX_BRUTE_FORCE_LENGTH = 16;
#endif

static const int BINARY = 0;
static const int BINARY_TEXT = 1;
static const int BINARY_IGNORE = 2;

typedef const char *(*indexer)(const char *, const char *, const uint8_t, const uint8_t);

typedef int (*comparer)(const char *, const char *, size_t);

static const int wordchars[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, // 0 - 9
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // a - o
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, // p - z, _
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // A - O
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, // P - Z
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

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
    const char *buf;
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
    int binary_mode;
    int lf;
    int table[256];
    uint64_t falses;
    char *lu;
    char *ll;
    bool ignore_case;
    uint8_t _case_mask8;
    uint16_t _case_mask16;
    bool use_regex;
    regex_t rx;
    struct rx_pattern_info rx_info;
    struct _grepper_file file;
};

void grepper_init(struct grepper *g, const char *find, bool ignore_case);

struct grepper *grepper_add(struct grepper *g, const char *find);

void grepper_free(struct grepper *g);

int64_t grepper_find(struct grepper *g, const char *s, int64_t ls);

int grepper_file(struct grepper *g, const char *path, int64_t size, void *memo);

const char *indexbyte(const char *s, const char *end, const uint8_t a);

struct rx_pattern_info rx_extract_plain(const char *s);

#endif
