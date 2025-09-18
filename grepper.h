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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#if defined(__x86_64__)
#include <emmintrin.h>
#include <immintrin.h>
#include <smmintrin.h>  
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

#include "stack.h"
#include "vendor.h"
#include "pcre2/deps/sljit/sljit_src/sljitLir.h"

#define DEFAULT_BUFFER_CAP 65536

#define BINARY            0
#define BINARY_TEXT       1
#define BINARY_IGNORE     2

#define OPEN_EMPTY        -98
#define OPEN_BINARY_SKIPPED -100

#define FILL_OK           -1
#define FILL_LAST_CHUNK   -8
#define FILL_EOF          -9

#define INIT_OK           0
#define INIT_INVALID_UTF8 -2

#define STATUS_QUEUED     1
#define STATUS_OPENED     2
#define STATUS_IS_DIR     32
#define STATUS_IS_BINARY  64

#define INC_NEXT          0
#define INC_FREEABLE      -1
#define INC_WAIT_FREEABLE -2

#define FILE_LOCK()    pthread_mutex_lock(&file->lock)
#define FILE_UNLOCK()  pthread_mutex_unlock(&file->lock)
#define PRINT_LOCK()   pthread_mutex_lock(&flags.lock)
#define PRINT_UNLOCK() pthread_mutex_unlock(&flags.lock)
#define LOCK_MSG(l, f) if (flags.quiet <= l) { PRINT_LOCK(); f; PRINT_UNLOCK(); }
#define DBG(msg, ...)  LOCK_MSG(-2, printf(msg, ##__VA_ARGS__));
#define LOG(msg, ...)  LOCK_MSG(-1, printf(msg, ##__VA_ARGS__));
#define WARN(msg, ...) LOCK_MSG(0, fprintf(stderr, msg, ##__VA_ARGS__));
#define ERR0(msg, ...) LOCK_MSG(1, fprintf(stderr, msg "\n", ##__VA_ARGS__));
#define ERR(msg, ...)  LOCK_MSG(1, fprintf(stderr, msg ": %s\n", ##__VA_ARGS__, strerror(errno)));

#ifdef _WIN32
#define PATH_SEP '\\'
#define _JOIN_PATH_IS_ABS(b, bl) ((bl) >= 3 && (b)[2] == '\\' && (b)[1] == ':' && isalpha((b)[0]))
#else
#define PATH_SEP '/'
#define _JOIN_PATH_IS_ABS(b, bl) ((bl) >= 1 && (b)[0] == '/')
#endif 

#define _JOIN_PATH(ALLOC, n, a, b, al, bl) do {\
    size_t ln = (al);                    \
    size_t ln2 = (bl);                   \
    n = (char *)ALLOC(ln + 1 + ln2 + 1); \
    if _JOIN_PATH_IS_ABS(b, ln2) { memcpy(n, b, ln2 + 1); break; } \
    memcpy(n, a, ln + 1);                \
    if (ln2 == 1 && b[0] == '.') break;  \
    if (n[ln - 1] == PATH_SEP) ln--; else n[ln] = PATH_SEP; \
    memcpy(n + ln + 1, b, ln2 + 1);      \
} while(0)
#define JOIN_PATH(n, a, b, bl) _JOIN_PATH(malloc, n, a, b, strlen(a), bl)
#define JOIN_PATH_TMP(n, a, b) char *n; _JOIN_PATH(alloca, n, a, b, strlen(a), strlen(b))

typedef sljit_sw (SLJIT_FUNC *func2_t)(sljit_sw a, sljit_sw b);

struct matcher {
    struct stacknode node;
    char *root;
    const char *file;
    struct strings includes;
    struct strings excludes;
    struct strings negate_excludes;
    struct matcher *parent;
    struct matcher *top;
    bool no_ignore;
};

struct grepfile;

struct grepline {
    struct grepper *g;
    struct grepfile_chunk *chunk;
    int64_t nr;
    bool is_ctxline;
    const char *line;
    int64_t len;
    int64_t match_start;
    int64_t match_end;
};

struct grepper {
    // Options
    bool ignore_case;
    bool search_name;
    bool disable_unicode;
    int binary_mode;
    int before_lines;
    int after_lines;
    bool (*callback)(const struct grepline *);

    // Internals
    pcre2_code *re;
    char *rx_error;
    char *find;
    char *findupper;
    char *findlower;
    size_t len;
    bool fixed;
    struct grepper *next_g;
    func2_t cmp;
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
    pthread_mutex_t lock;
};

struct grepfile_chunk {
    int64_t prev_lines;
    size_t off;
    char *buf;
    ssize_t buf_size;
    ssize_t data_size;
    ssize_t cap_size;
    struct grepfile *file;
    pcre2_match_data *match_data;
};

struct worker {
    pthread_t thread;
    int32_t _Atomic *actives;
    uint8_t tid;
    struct grepfile_chunk chunk;
};

struct _Flags {
    char cwd[PATH_MAX];
    bool color;
    bool fixed_string;
    bool no_symlink;
    bool no_ignore;
    bool imm_open;
    int max_imm_openfiles;
    int num_threads;
    int quiet;
    int xbytes;
    int verbose;
    pthread_mutex_t lock; 
    int64_t _Atomic ignores;
    int64_t _Atomic files;
};

extern pthread_mutex_t empty_mutex;

extern struct _Flags flags;

void print_flush();

bool print_callback(const struct grepline *l);

const char *is_glob_path(const char *p, const char *end);

const char *rel_path(const char *a, const char *b);

bool is_repo_bin(const char *dir, const char *name);

bool is_dir(const char *name, bool follow_link);

bool matcher_match(struct matcher *m, const char *name, bool is_dir);

void matcher_free(void *m);

bool matcher_add_rule(struct matcher *m, const char *l, const char *end, bool incl);

struct matcher *matcher_load_ignore_file(const char *, struct matcher *, struct stack *);

int grepper_fixed(struct grepper *, const char *);

void grepper_create(struct grepper *, const char *);

void grepper_free(struct grepper *g);

int grepfile_open(struct grepper *, struct grepfile *, struct grepfile_chunk *);

int grepfile_release(struct grepfile *file);

int grepfile_inc_ref(struct grepfile *file);

void grepfile_dec_ref(struct grepfile *file);

int grepfile_acquire_chunk(struct grepper *, struct grepfile *, struct grepfile_chunk *);

void grepfile_process_chunk(struct grepper *, struct grepfile_chunk *);

int64_t countbyte(const char *s, const char *end, uint8_t c);

const char *indexbyte(const char *s, const char *end, const uint8_t a);

const char *indexlastbyte(const char *start, const char *s, const uint8_t a);

static inline int64_t MAX(int64_t a, int64_t b) { return a > b ? a : b; }

static inline int64_t MIN(int64_t a, int64_t b) { return a < b ? a : b; }

#endif

