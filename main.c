#include "stack.h"
#include "grepper.h"
#include "wildmatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <string.h>

#define PATH_SEP '/'
#define LOCK() if (flags.use_lock) { pthread_mutex_lock(&flags.lock); }
#define UNLOCK() if (flags.use_lock) { pthread_mutex_unlock(&flags.lock); }
#define LOG(msg, ...) if (flags.verbose) { printf(msg, ##__VA_ARGS__); }
#define ERR(msg, ...) if (flags.quiet <= 1) { LOCK(); fprintf(stderr, msg, ##__VA_ARGS__); \
    if (errno) fprintf(stderr, ": %s\n", strerror(errno)); else fprintf(stderr, "\n"); UNLOCK(); }
#define WARN(msg, ...) if (flags.quiet <= 0) { LOCK(); fprintf(stderr, msg, ##__VA_ARGS__); UNLOCK(); }

static struct _flags {
    bool verbose;
    bool ignore_case;
    bool color;
    bool fixed_string;
    int num_threads;
    int line_size;
    int quiet;
    int xbytes;
    bool use_lock;
    pthread_mutex_t lock; 
    struct stack includes;
    struct stack excludes;
    struct stack negate_excludes;
} flags;

struct payload {
    int i;
    uint32_t _Atomic *sigs;
    const char *current_file;
    struct stack *s;
    struct grepper *g;
    struct grepper_ctx ctx;
};

bool is_repo_bin(const char *dir, const char *name)
{
    const char *dot = strrchr(dir, '.');
    if (!dot)
        return false;
    if (strcmp(dot, ".git") == 0 && strcmp(name, "objects") == 0)
        return true;
    if (strcmp(dot, ".hg") == 0 && strcmp(name, "store") == 0)
        return true;
    return false;
}

char *join_path(const char *a, const char *b)
{
    char res[PATH_MAX];
    if (strlen(b) > 0 && b[0] == PATH_SEP) {
        memcpy(res, b, strlen(b) + 1);
    } else {
        memcpy(res, a, strlen(a));
        res[strlen(a)] = PATH_SEP;
        memcpy(res + strlen(a) + 1, b, strlen(b) + 1);
    }
    char *resolved = realpath(res, NULL);
    return resolved;
}

void *push(void *arg) 
{
    struct payload *p = (struct payload *)arg;
    char *name;
NEXT:
    if (!stack_pop(p->s, (void **)&name)) {
        p->sigs[p->i] = 0;
        for (int i = 0; i < flags.num_threads; i++) {
            if (atomic_load(&p->sigs[i])) {
                struct timespec req = {
                    .tv_sec = 0,
                    .tv_nsec = 10000000, // 10ms
                };
                nanosleep(&req, NULL);
                goto NEXT;
            }
        }
        return 0;
    }
    atomic_store(&p->sigs[p->i], 1);

    struct stat ss;
    lstat(name, &ss);

    size_t ln = strlen(name);
    ln = name[ln - 1] == '/' ? ln - 1 : ln;
    if (S_ISDIR(ss.st_mode)) {
        DIR *dir = opendir(name);
        if (dir == NULL) {
            ERR("open %s", name);
            goto CLEANUP;
        }
        struct dirent *dirent;
        while ((dirent = readdir(dir)) != NULL) {
            const char *dname = dirent->d_name;
            if (strcmp(dname, ".") == 0 || strcmp(dname, "..") == 0 || is_repo_bin(name, dname))
                continue;
            char *buf = (char *)malloc(ln + 1 + strlen(dname) + 1);
            if (strcmp(name, ".") == 0) {
                memcpy(buf, dname, strlen(dname) + 1);
            } else {
                memcpy(buf, name, ln);
                memcpy(buf + ln, "/", 1);
                memcpy(buf + ln + 1, dname, strlen(dname) + 1);
            }
            stack_push(p->s, buf);
        }
        closedir(dir);
    } else if (S_ISREG(ss.st_mode)) {
        bool incl = flags.includes.count == 0;
        for (struct stacknode *n = flags.includes.root; n; n = n->next) {
            if (wildmatch(n->value, name, 0) == WM_MATCH) {
                incl = true;
                break;
            }
        }
        for (struct stacknode *n = flags.excludes.root; n; n = n->next) {
            if (wildmatch(n->value, name, WM_PATHNAME) == WM_MATCH) {
                for (struct stacknode *n = flags.negate_excludes.root; n; n = n->next) {
                    if (wildmatch(n->value, name, 0) == WM_MATCH)
                        goto NEGATED;
                }
                incl = false;
                break;
NEGATED:
            }
        }
        if (incl) {
            p->current_file = name;
            int res = grepper_file(p->g, name, ss.st_size, &p->ctx);
            if (res != 0) {
                ERR("read %s", name);
            }
            if (p->ctx.lbuf.overflowed && !p->ctx.lbuf.is_binary && flags.quiet == 0) {
                WARN("%s has long line >%dK, matches may be incomplete\n", name, flags.line_size / 1024);
            }
        }
    }

CLEANUP:
    free(name);
    goto NEXT;
}

void colorprint(const char *name, int64_t nr, const char *line, int start, int end, int len,
        const char *labbr, const char *rabbr) 
{
    printf("\033[0;35m%s\033[0m:\033[1;32m%lld\033[0m:%s%.*s\033[1;31m%.*s\033[0m%.*s%s\n",
            name, nr,
            labbr,
            start, line, end - start, line + start, len - end, line + end,
            rabbr);
}

bool grep_callback(const struct grepline *l)
{
    struct payload *p = (struct payload *)l->ctx->memo;
    const char *name = p->current_file;
    LOCK();
    int limit = flags.xbytes + p->g->len + 10;
    if (l->ctx->lbuf.is_binary && p->g->binary_mode == BINARY) {
        printf("%s: binary file matches\n", name);
    } else if (l->is_afterline || !flags.color) {
        if (l->len <= limit) {
            printf("%s:%lld:%.*s\n", name, l->nr + 1, l->len, l->line);
        } else {
            int i = MAX(0, l->match_start - flags.xbytes / 2);
            int j = MIN(l->len, l->match_end + flags.xbytes /2);
            for (; i > 0 && ((uint8_t)l->line[i] >> 6) == 2; i--);
            for (; j < l->len && ((uint8_t)l->line[j] >> 6) == 2; j++);
            printf("%s:%lld:...%.*s...\n", name, l->nr + 1, j - i, l->line + i);
        }
    } else {
        if (l->len <= limit) {
            colorprint(name, l->nr + 1, l->line, l->match_start, l->match_end, l->len, "", "");
        } else {
            int i = MAX(0, l->match_start - flags.xbytes / 2);
            int j = MIN(l->len, l->match_end + flags.xbytes / 2);
            for (; i > 0 && ((uint8_t)l->line[i] >> 6) == 2; i--);
            for (; j < l->len && ((uint8_t)l->line[j] >> 6) == 2; j++);
            colorprint(name, l->nr + 1, l->line + i, l->match_start - i, l->match_end - i, j - i,
                    i == 0 ? "" : "\033[1;33m...\033[0m", j == l->len ? "" : "\033[1;33m...\033[0m");
        }
    }
    UNLOCK();
    return true;
}

void load_ignore_file(const char *path)
{
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
    FILE * fp = fopen(path, "r");
    if (fp == NULL) {
        ERR("failed to load ignore file %s", path);
        return;
    }

    LOG("* load ignore file %s\n", path);
    while ((read = getline(&line, &len, fp)) != -1) {
        if (read < 2 || line[0] == '#' || (line[0] == '!' && read == 2))
            continue;
        line[--read] = 0;
        char *buf = (char *)malloc(read + 10);
        bool negate = false;
        if (line[0] == '!') {
            memcpy(buf, line + 1, read);
            negate = true;
        } else {
            memcpy(buf, line, read + 1);
        }
        if (buf[strlen(buf) - 1] == '/')
            memcpy(buf + strlen(buf), "**", 3);
        if (buf[0] == '/')
            memcpy(buf, buf + 1, strlen(buf));
        stack_push(negate ? &flags.negate_excludes : &flags.excludes, buf);
        LOG("* %sexclude pattern %s from ignore file\n", !negate ? "" : "negate ", buf, path);
    }

    fclose(fp);
    if (line)
        free(line);
}

void usage()
{
    printf("usage: simdgrep [OPTIONS]... PATTERN FILES...\n");
    printf("PATTERN syntax is POSIX extended regex\n");
    printf("FILE starting with '+' is an include pattern, e.g.: simdgrep PATTERN dir +*.c\n");
    printf("FILE starting with '-' is an exclude pattern, e.g.: simdgrep PATTERN dir -testdata\n");
    printf("\n");
    printf("\t-F\tfixed string pattern\n");
    printf("\t-Z\tmatch case sensitively (insensitive by default)\n");
    printf("\t-P\tprint results without coloring\n");
    printf("\t-q\tsuppress warning messages\n");
    printf("\t-qq\tsuppress error messages\n");
    printf("\t-a\ttreat binary as text\n");
    printf("\t-I\tignore binary files\n");
    printf("\t-x<NUM>\ttruncate lines longer than NUM bytes to make output compact\n");
    printf("\t-A<NUM>\tprint NUM lines of trailing context\n");
    printf("\t-M<NUM>\tdefine max line length in NUM kilobytes, any lines longer will be split,\n");
    printf("\t       \tthus matches may be incomplete at split points (default: 64K)\n");
    printf("\t-J<NUM>\tNUM of threads for searching\n");
    printf("\t-V\tdebug ouput\n");
}

int main(int argc, char **argv) 
{
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage();
        return 0;
    }

    if (pthread_mutex_init(&flags.lock, NULL) != 0) { 
        ERR("simdgrep can't start");
        return 0; 
    } 
    flags.verbose = flags.fixed_string = false;
    flags.color = isatty(STDOUT_FILENO);
    flags.ignore_case = flags.use_lock = true;
    flags.line_size = 65536;
    flags.quiet = 0;
    flags.xbytes = 1e8;
    flags.negate_excludes.root = flags.excludes.root = flags.includes.root = NULL;
    flags.negate_excludes.count = flags.excludes.count = flags.includes.count = 0;
    flags.num_threads = sysconf(_SC_NPROCESSORS_ONLN);

    struct stack s;
    s.root = NULL;
    s.count = 0;

    struct grepper g;
    g.find = NULL;
    g.file.after_lines = 0;
    g.file.callback = grep_callback;
    g.binary_mode = BINARY;
    g.use_regex = false;

    const char *expr = argv[1];
    int arg_files = 2;
    for (int i = 1; i < argc - 1 && argv[i][0] == '-'; i++) {
        expr = argv[i + 1];
        arg_files = i + 2;
        for (int j = 1; j < strlen(argv[i]); j++) {
            switch (argv[i][j]) {
            case 'F': flags.fixed_string = true; break;
            case 'V': flags.verbose = true; break;
            case 'Z': flags.ignore_case = false; break;
            case 'P': flags.color = false; break; 
            case 'q': flags.quiet++; break;
            case 'a': g.binary_mode = BINARY_TEXT; break;
            case 'I': g.binary_mode = BINARY_IGNORE; break;
            case 'M':
                      flags.line_size = MAX(1, strtol(argv[i] + j + 1, NULL, 10)) * 1024;
                      j = strlen(argv[i]);
                      break;
            case 'A': case 'x':
                      *(argv[i][j] == 'A' ? &g.file.after_lines : &flags.xbytes) = MAX(0, strtol(argv[i] + j + 1, NULL, 10));
                      j = strlen(argv[i]);
                      break;
            case 'j':
                      flags.num_threads = MAX(1, strtol(argv[i] + j + 1, NULL, 10));
                      j = strlen(argv[i]);
                      break;
            }
        }
    }
    
    LOG("* search pattern: '%s', arg files: %d\n", expr, argc - arg_files);
    LOG("* line size: %dK\n", flags.line_size / 1024);
    LOG("* use %d threads\n", flags.num_threads);
    LOG("* binary mode: %d\n", g.binary_mode);
    LOG("* print after %d lines\n", g.file.after_lines);
    LOG("* line context bytes +-%d\n", flags.xbytes);

    struct payload payloads[flags.num_threads];
    pthread_t threads[flags.num_threads];
    uint32_t _Atomic sigs[flags.num_threads];

    if (flags.fixed_string) {
        g.rx_info.fixed_patterns = strdup(expr);
        g.rx_info.fixed_len = strlen(expr);
        g.rx_info.pure = true;
    } else {
        g.rx_info = rx_extract_plain(expr);
        if (g.rx_info.unsupported_escape) {
            ERR("pattern with unsupported escape at '%s'", g.rx_info.unsupported_escape);
            goto EXIT;
        }
    }

    for (int i = 0 ; i < g.rx_info.fixed_len; ) {
        int ln = strlen(g.rx_info.fixed_patterns + i);
        if (ln) {
            LOG("* fixed %s: %s\n", !g.find && g.rx_info.fixed_start ? "prefix" : "pattern", g.rx_info.fixed_patterns + i);
            if (!g.find) {
                grepper_init(&g, g.rx_info.fixed_patterns + i, flags.ignore_case);
            } else {
                grepper_add(&g, g.rx_info.fixed_patterns + i);
            }
        }
        i += ln + 1;
    }
    if (!g.find) {
        WARN("warning: no fixed pattern in '%s', searching will be extremely slow\n", expr);
        grepper_init(&g, "\n", flags.ignore_case);
    }

    if (!g.rx_info.pure) {
        LOG("* enabled regex\n");
        g.use_regex = true;
        int rc = regcomp(&g.rx, expr, REG_EXTENDED | (flags.ignore_case ? REG_ICASE : 0));
        if (rc) {
            char buffer[1024];
            regerror(rc, &g.rx, buffer, 1024);
            ERR("invalid expression '%s': %s", expr, buffer);
            goto CLEANUP;
        }
    }

    for (; arg_files < argc; arg_files++) {
        const char *name = argv[arg_files];
        if (strlen(name) == 0)
            continue;
        if (name[0] == '-') {
            stack_push(&flags.excludes, strdup(name + 1));
            LOG("* exclude pattern %s\n", name + 1);
        } else if (name[0] == '+') {
            stack_push(&flags.includes, strdup(name + 1));
            LOG("* include pattern %s\n", name + 1);
        } else if (name[0] == '^') {
            load_ignore_file(name + 1);
        } else {
            LOG("* search %s\n", name);
            stack_push(&s, strdup(name));
        }
    }
    if (s.count == 0) {
        stack_push(&s, strdup("."));
        LOG("* search current working directory\n");
    }
    for (int i = 0; i < flags.num_threads; ++i) {
        payloads[i].i = i;
        payloads[i].g = &g;
        payloads[i].s = &s;
        payloads[i].sigs = sigs;
        payloads[i].ctx.memo = &payloads[i];
        buffer_init(&payloads[i].ctx.lbuf, flags.line_size);
        pthread_create(&threads[i], NULL, push, (void *)&payloads[i]);
    }
    for (int i = 0; i < flags.num_threads; ++i) {
        pthread_join(threads[i], NULL);
        buffer_free(&payloads[i].ctx.lbuf);
    }

    int num_files = stack_free(&s);
    LOG("* searched %d files\n", num_files);
    // fprintf(stderr, "%llu\n", g.falses);

CLEANUP:
    grepper_free(&g);

EXIT:
    stack_free(&flags.includes);
    stack_free(&flags.excludes);
    stack_free(&flags.negate_excludes);
    pthread_mutex_destroy(&flags.lock); 
    return 0;
}
