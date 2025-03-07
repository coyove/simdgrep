#include "stack.h"
#include "grepper.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <string.h>

#define LOG(msg, ...) if (flags.verbose) { printf(msg, ##__VA_ARGS__); }

static struct _flags {
    bool verbose;
    bool ignore_case;
    bool color;
    int num_threads;
    int line_size;
} flags = {
    .verbose = false,
    .ignore_case = true,
    .line_size = 65536,
    .color = true,
};

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
            fprintf(stderr, "open %s: ", name);
            perror("");
            goto CLEANUP;
        }
        struct dirent *dirent;
        while ((dirent = readdir(dir)) != NULL) {
            const char *dname = dirent->d_name;
            if (strcmp(dname, ".") == 0 || strcmp(dname, "..") == 0 || is_repo_bin(name, dname))
                continue;
            char *buf = (char *)malloc(ln + 1 + strlen(dname) + 1);
            memcpy(buf, name, ln);
            memcpy(buf + ln, "/", 1);
            memcpy(buf + ln + 1, dname, strlen(dname) + 1);
            stack_push(p->s, buf);
        }
        closedir(dir);
    } else if (S_ISREG(ss.st_mode)) {
        p->current_file = name;
        int res = grepper_file(p->g, name, ss.st_size, &p->ctx);
        if (res != 0)  {
            fprintf(stderr, "read %s: ", name);
            perror("");
        }
        if (p->ctx.lbuf.overflowed && !p->ctx.lbuf.is_binary) {
            fprintf(stderr, "%s has long line >%dK, matches may be incomplete\n", name, flags.line_size / 1024);
        }
    }

CLEANUP:
    free(name);
    goto NEXT;
}

bool grep_callback(const struct grepline *l) {
    struct payload *p = (struct payload *)l->ctx->memo;
    const char *name = p->current_file;
    if (l->ctx->lbuf.is_binary) {
        printf("%s: binary file matches\n", name);
        return true;
    }
    if (l->len < 1 << 20) {
        if (!flags.color) {
            printf("%s:%lld:%.*s\n", name, l->nr + 1, l->len, l->line);
        } else {
            printf("\033[1;35m%s\033[0m:\033[1;32m%lld\033[0m:%.*s\033[1;31m%.*s\033[0m%.*s\n",
                    name, l->nr + 1,
                    l->match_start, l->line,
                    l->match_end - l->match_start, l->line + l->match_start,
                    l->len - l->match_end, l->line + l->match_end
                  );
        }
    }
    return true;
}

int main(int argc, char **argv) 
{
    if (argc < 2) {
        printf("usage: simdgrep [OPTIONS]... PATTERN FILES...");
        return 0;
    }

    flags.num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    flags.color = isatty(STDOUT_FILENO);

    struct stack s;
    s.root = NULL;
    s.count = 0;

    struct grepper g;
    g.file.after_lines = 0;
    g.file.callback = grep_callback;
    g.binary_mode = BINARY_IGNORE;
    g.use_regex = false;

    const char *expr = argv[1];
    int arg_files = MAX(2, argc - 1);
    for (int i = 1; i < argc - 1 && argv[i][0] == '-'; i++) {
        expr = argv[i + 1];
        arg_files = i + 2;
        for (int j = 1; j < strlen(argv[i]); j++) {
            switch (argv[i][j]) {
            case 'V': flags.verbose = true; break;
            case 'Z': flags.ignore_case = false; break;
            case 'P': flags.color = false; break; 
            case 'a': g.binary_mode = BINARY_TEXT;
            case 'I': g.binary_mode = BINARY_IGNORE;
            case 'M':
                      flags.line_size = MAX(1, strtol(argv[i] + j + 1, NULL, 10)) * 1024;
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

    bool g_inited = false;
    g.rx_info = rx_extract_plain(expr);
    if (g.rx_info.unsupported_escape) {
        fprintf(stderr, "pattern with unsupported escape at '%s'\n", g.rx_info.unsupported_escape);
        return 1;
    }

    for (int i = 0 ; i < g.rx_info.fixed_len; ) {
        int ln = strlen(g.rx_info.fixed_patterns + i);
        if (ln) {
            LOG("* %sfixed pattern: %s\n", !g_inited && g.rx_info.fixed_start ? "start " : "", g.rx_info.fixed_patterns + i);
            if (!g_inited) {
                grepper_init(&g, g.rx_info.fixed_patterns + i, flags.ignore_case);
                g_inited = true;
            } else {
                grepper_add(&g, g.rx_info.fixed_patterns + i);
            }
        }
        i += ln + 1;
    }
    if (!g_inited) {
        LOG("* no fixed pattern found, require full scan\n");
        grepper_init(&g, "\n", flags.ignore_case);
    }

    if (!g.rx_info.pure) {
        LOG("* enabled regex\n");
        g.use_regex = true;
        int rc = regcomp(&g.rx, expr, REG_EXTENDED | REG_ICASE);
        if (rc) {
            char buffer[1024];
            regerror(rc, &g.rx, buffer, 1024);
            fprintf(stderr, "invalid expression '%s': %s", expr, buffer);
            return 1;
        }
    }

    if (arg_files >= argc) {
        stack_push(&s, strdup("."));
        LOG("* search current working directory\n");
    } else {
        for (; arg_files < argc; arg_files++) {
            LOG("* search %s\n", argv[arg_files]);
            stack_push(&s, strdup(argv[arg_files]));
        }
    }

    struct payload payloads[flags.num_threads];
    pthread_t threads[flags.num_threads];
    uint32_t _Atomic sigs[flags.num_threads];
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

    grepper_free(&g);
    int num_files = stack_free(&s);
    LOG("* searched %d files\n", num_files);
    // fprintf(stderr, "%llu\n", g.falses);
    return 0;
}
