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

typedef int (*comparer)(const char *, const char *, size_t);

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
    uint64_t _Atomic falses;
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

struct grepper_ctx {
    void *memo;
    char *buf;
    size_t buf_len;
};

void grepper_init(struct grepper *g, const char *find, bool ignore_case);

struct grepper *grepper_add(struct grepper *g, const char *find);

void grepper_free(struct grepper *g);

int64_t grepper_find(struct grepper *g, const char *s, int64_t ls);

int grepper_file(struct grepper *, const char *, int64_t, struct grepper_ctx *);

const char *indexbyte(const char *s, const char *end, const uint8_t a);

struct rx_pattern_info rx_extract_plain(const char *s);

#endif
