#include "stack.h"
#include "grepper.h"
#include "wildmatch.h"
#include "pathutil.h"
#include "STC/include/stc/priv/utf8_prv.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>

#define LOCK() pthread_mutex_lock(&flags.lock)
#define UNLOCK() pthread_mutex_unlock(&flags.lock)
#define LOG(msg, ...) if (flags.quiet < 0) { printf(msg, ##__VA_ARGS__); }
#define ERR(msg, ...) if (flags.quiet <= 1) { LOCK(); fprintf(stderr, msg, ##__VA_ARGS__); \
    if (errno) fprintf(stderr, ": %s\n", strerror(errno)); else fprintf(stderr, "\n"); UNLOCK(); }
#define WARN(msg, ...) if (flags.quiet <= 0) { LOCK(); fprintf(stderr, msg, ##__VA_ARGS__); UNLOCK(); }

static struct _flags {
    char cwd[PATH_MAX];
    bool ignore_case;
    bool color;
    bool fixed_string;
    bool no_ignore;
    int num_threads;
    int line_size;
    int quiet;
    int xbytes;
    pthread_mutex_t lock; 
    struct matcher default_matcher;
    int64_t _Atomic ignores;
} flags;

struct payload {
    pthread_t thread;
    int i;
    uint32_t _Atomic *sigs;
    struct grepper_ctx ctx;
};

struct task {
    char *name;
    struct stat stat;
    struct matcher *m;
};

struct task *new_task(char *name, struct matcher *m)
{
    struct task *t = (struct task *)malloc(sizeof(struct task));
    t->name = name;
    t->m = m;
    return t;
}

struct stack tasks;
struct stack files;
struct stack matchers;
struct grepper g;

void *push(void *arg) 
{
    struct payload *p = (struct payload *)arg;
    struct task *t;
    struct stat ss;
    const char *rule, *source;
    char reason[1024];

NEXT:
    if (!stack_pop(&tasks, (void **)&t)) {
        if (!stack_pop(&files, (void **)&t)) {
            p->sigs[p->i] = 0;
            for (int i = 0; i < flags.num_threads; i++) {
                if (atomic_load(&p->sigs[i])) {
                    struct timespec req = { .tv_sec = 0, .tv_nsec = 1000000 /* 1ms */ };
                    nanosleep(&req, NULL);
                    goto NEXT;
                }
            }
            return 0;
        }
        atomic_store(&p->sigs[p->i], 1);

        p->ctx.file_name = t->name;
        p->ctx.file_size = t->stat.st_size;
        int res = grepper_file(&g, &p->ctx);
        if (res != 0) {
            ERR("read %s", t->name);
        }
        if (p->ctx.lbuf.overflowed && !p->ctx.lbuf.is_binary && flags.quiet == 0) {
            WARN("%s has long line >%dK, matches may be incomplete\n", t->name, flags.line_size / 1024);
        }
        goto CLEANUP_TASK;
    }
    atomic_store(&p->sigs[p->i], 1);

    if (lstat(t->name, &ss) != 0) {
        ERR("stat %s", t->name);
        goto CLEANUP_TASK;
    }

    if (S_ISDIR(ss.st_mode)) {
        size_t ln = strlen(t->name);
        ln = t->name[ln - 1] == '/' ? ln - 1 : ln;
        if (!matcher_match(t->m, t->name, true, reason, sizeof(reason))) {
            atomic_fetch_add(&flags.ignores, 1);
            LOG("ignore directory %s\n", reason);
            goto CLEANUP_TASK;
        }

        struct matcher *root = t->m;
        if (!flags.no_ignore) {
            struct matcher *m = matcher_load_ignore_file(t->name, root, &matchers);
            if (m) {
                LOG("load ignore file from %s\n", m->root);
                root = m;
            }
        }

        DIR *dir = opendir(t->name);
        if (dir == NULL) {
            ERR("open %s", t->name);
            goto CLEANUP_TASK;
        }
        struct dirent *dirent;
        while ((dirent = readdir(dir)) != NULL) {
            const char *dname = dirent->d_name;
            if (strcmp(dname, ".") == 0 || strcmp(dname, "..") == 0)
                continue;
            char *buf = (char *)malloc(ln + 1 + strlen(dname) + 1);
            if (strcmp(t->name, ".") == 0) {
                memcpy(buf, dname, strlen(dname) + 1);
            } else {
                memcpy(buf, t->name, ln);
                memcpy(buf + ln, "/", 1);
                memcpy(buf + ln + 1, dname, strlen(dname) + 1);
            }
            stack_push(&tasks, new_task(buf, root));
        }
        closedir(dir);
    } else if (S_ISREG(ss.st_mode) && ss.st_size > 0) {
        t->stat = ss;
        if (matcher_match(t->m, t->name, false, reason, sizeof(reason))) {
            stack_push(&files, t);
            goto NEXT;
        }
        atomic_fetch_add(&flags.ignores, 1);
        LOG("ignore file %s\n", reason);
    }

CLEANUP_TASK:
    free(t->name);
    free(t);
    goto NEXT;
}

bool grep_callback(const struct grepline *l)
{
    // return true;
    struct payload *p = (struct payload *)l->ctx->memo;
    const char *name = rel_path(flags.cwd, l->ctx->file_name);
    LOCK();
    if (l->ctx->lbuf.is_binary && g.binary_mode == BINARY) {
        printf("%s: binary file matches\n", name);
    } else {
        int i = 0, j = l->len;
        if (l->len > flags.xbytes + g.len + 10) {
            i = MAX(0, l->match_start - flags.xbytes / 2);
            j = MIN(l->len, l->match_end + flags.xbytes / 2);
            for (; i > 0 && ((uint8_t)l->line[i] >> 6) == 2; i--);
            for (; j < l->len && ((uint8_t)l->line[j] >> 6) == 2; j++);
        }

        if (l->is_ctxline || !flags.color) {
            printf("%s:"
                    "%lld:"
                    "%s"
                    "%.*s"
                    "%s\n",
                    name, l->nr + 1,
                    i == 0 ? "" : "...",
                    j - i, l->line + i,
                    j == l->len ? "" : "...");
        } else {
            printf("\033[0;35m%s\033[0m:"    // filename
                    "\033[1;32m%lld\033[0m:" // line number
                    "\033[1;33m%s\033[0m"    // left ellipsis
                    "%.*s"                   // before context
                    "\033[1;31m%.*s\033[0m"  // hightlight match
                    "%.*s"                   // after context
                    "\033[1;33m%s\033[0m\n", // right ellipsis
                    name, l->nr + 1,
                    i == 0 ? "" : "...",
                    l->match_start - i, l->line + i,
                    l->match_end - l->match_start, l->line + l->match_start,
                    j - l->match_end, l->line + l->match_end,
                    j == l->len ? "" : "...");
        }
    }
    UNLOCK();
    return true;
}

char *join_cwd_or_die(const char *b)
{
    char *resolved = join_path(flags.cwd, b);
    if (!resolved) {
        ERR("can't resolve path %s", b);
        exit(0);
    }
    return resolved;
}

void usage()
{
    printf("usage: simdgrep [OPTIONS]... PATTERN FILES...\n");
    printf("PATTERN syntax is POSIX extended regex\n");
    printf("FILE starting with '+' is an include pattern, e.g.: simdgrep PATTERN dir +*.c\n");
    printf("FILE starting with '-' is an exclude pattern, e.g.: simdgrep PATTERN dir -**/testdata\n");
    printf("\n");
    printf("\t-F\tfixed string pattern\n");
    printf("\t-Z\tmatch case sensitively (insensitive by default)\n");
    printf("\t-P\tprint results without coloring\n");
    printf("\t-G\tsearch all files regardless of .gitignore\n");
    printf("\t-q\tsuppress warning messages\n");
    printf("\t-qq\tsuppress error messages\n");
    printf("\t-a\ttreat binary as text\n");
    printf("\t-I\tignore binary files\n");
    printf("\t-x NUM\ttruncate lines longer than NUM bytes to make output compact\n");
    printf("\t-A NUM\tprint NUM lines of trailing context\n");
    printf("\t-B NUM\tprint NUM lines of leading context\n");
    printf("\t-C NUM\tcombine -A<NUM> and -B<NUM>\n");
    printf("\t-M NUM\tdefine max line length in NUM kilobytes, any lines longer will be split,\n");
    printf("\t       \tthus matches may be incomplete at split points (default: 64K)\n");
    printf("\t-J NUM\tNUM of threads for searching\n");
    printf("\t-V\tdebug ouput\n");
    abort();
}

int main(int argc, char **argv) 
{
    // char zzz[100] = {137,82,176,12,216,99,190,52,239,115,92,199,0};
    // utf8_decode_t ctx = {.state=0};
    // const uint8_t *b = (const uint8_t*)zzz;
    // while (*b) {
    // do { printf("%c\n", *b); utf8_decode(&ctx, *b++); } while (ctx.state && ctx.state != 12);
    // printf("=%d\n", ctx.codep);
    // }
 
    // return 0;
    if (pthread_mutex_init(&flags.lock, NULL) != 0) { 
        ERR("simdgrep can't start");
        return 0; 
    } 
    flags.fixed_string = flags.no_ignore = false;
    flags.color = isatty(STDOUT_FILENO);
    flags.ignore_case = true;
    flags.line_size = 65536;
    flags.quiet = 0;
    flags.xbytes = 1e8;
    memset(&flags.default_matcher, 0, sizeof(struct matcher));
    flags.default_matcher.top = &flags.default_matcher;
    flags.num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    getcwd(flags.cwd, sizeof(flags.cwd));

    memset(&tasks, 0, sizeof(tasks));
    memset(&matchers, 0, sizeof(tasks));
    memset(&g, 0, sizeof(g));
    g.callback = grep_callback;

    int cop;
    opterr = 0;
    while ((cop = getopt(argc, argv, "hFVZPqGaIM:A:B:C:x:j:")) != -1) {
        switch (cop) {
        case 'F': flags.fixed_string = true; break;
        case 'V': flags.quiet = -1; break;
        case 'Z': flags.ignore_case = false; break;
        case 'P': flags.color = false; break; 
        case 'q': flags.quiet++; break;
        case 'G': flags.no_ignore = true; break;
        case 'a': g.binary_mode = BINARY_TEXT; break;
        case 'I': g.binary_mode = BINARY_IGNORE; break;
        case 'M': flags.line_size = MAX(1, atoi(optarg)) * 1024; break;
        case 'A': g.after_lines = MAX(0, atoi(optarg)); break;
        case 'B': g.before_lines = MAX(0, atoi(optarg)); break;
        case 'C': g.after_lines = g.before_lines = MAX(0, atoi(optarg)); break;
        case 'x': flags.xbytes = MAX(0, atoi(optarg)); break;
        case 'j': flags.num_threads = MAX(1, atoi(optarg)); break;
        default: usage();
        }
    }
    if (optind >= argc)
        usage();

    const char *expr = argv[optind];
    if (strlen(expr) == 0)
        return 0;
    
    LOG("* search pattern: '%s', arg files: %d\n", expr, argc - optind - 1);
    LOG("* line size: %dK\n", flags.line_size / 1024);
    LOG("* use %d threads\n", flags.num_threads);
    LOG("* binary mode: %d\n", g.binary_mode);
    LOG("* print after %d lines\n", g.after_lines);
    LOG("* line context bytes +-%d\n", flags.xbytes);

    struct payload payloads[flags.num_threads];
    uint32_t _Atomic sigs[flags.num_threads];

    if (flags.fixed_string) {
        grepper_init(&g, expr, flags.ignore_case);
    } else {
        grepper_init_rx(&g, expr, flags.ignore_case);
        if (g.rx.unsupported_escape) {
            ERR("pattern with unsupported escape at '%s'", g.rx.unsupported_escape);
            goto EXIT;
        }
        if (g.rx.error) {
            ERR("invalid expression: %s", g.rx.error);
            goto EXIT;
        }
        for (struct grepper *ng = &g; ng; ng = ng->next_g) {
            LOG("* fixed %s: %s\n", ng == &g && g.rx.fixed_start ? "prefix" : "pattern", ng->find);
        }
        if (strcmp(g.find, "\n") == 0) {
            WARN("warning: no fixed pattern in '%s', searching will be extremely slow\n", expr);
        }
    }

    for (int arg_files = optind + 1; arg_files < argc; arg_files++) {
        const char *name = argv[arg_files];
        if (strlen(name) == 0)
            continue;
        if (name[0] == '+') {
            matcher_add_rule(&flags.default_matcher, name + 1, name + strlen(name), true);
            LOG("* add include pattern %s\n", name + 1);
        } else if (name[0] == '-') {
            matcher_add_rule(&flags.default_matcher, name + 1, name + strlen(name), false);
            LOG("* add exclude pattern %s\n", name + 1);
        } else {
            LOG("* search %s\n", name);
            stack_push(&tasks, new_task(join_cwd_or_die(name), &flags.default_matcher));
        }
    }
    if (tasks.count == 0) {
        stack_push(&tasks, new_task(join_cwd_or_die("."), &flags.default_matcher));
        LOG("* search current working directory\n");
    }
    for (int i = 0; i < flags.num_threads; ++i) {
        payloads[i].i = i;
        payloads[i].sigs = sigs;
        payloads[i].ctx.memo = &payloads[i];
        buffer_init(&payloads[i].ctx.lbuf, flags.line_size);
        pthread_create(&payloads[i].thread, NULL, push, (void *)&payloads[i]);
    }
    for (int i = 0; i < flags.num_threads; ++i) {
        pthread_join(payloads[i].thread, NULL);
        buffer_free(&payloads[i].ctx.lbuf);
    }

    int num_walks = stack_free(&tasks);
    int num_files = stack_free(&files);
    LOG("* walked %d entries, searched %d files\n", num_walks, num_files);

    FOREACH(&matchers, n) matcher_free((struct matcher *)n->value);
    int num_ignores = stack_free(&matchers);
    if (flags.no_ignore) {
        LOG("* all files are searched, no ignores\n");
    } else {
        LOG("* respected %d .gitignore, %lld files ignored\n", num_ignores, flags.ignores);
    }
    LOG("%lld falses\n", g.falses);

EXIT:
    grepper_free(&g);
    matcher_free(&flags.default_matcher);
    pthread_mutex_destroy(&flags.lock); 
    return 0;
}
