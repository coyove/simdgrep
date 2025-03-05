#include "stack.h"
#include "grepper.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <string.h>

static struct _flags {
    bool verbose;
    bool ignore_case;
    int num_threads;
} flags = {
    .ignore_case = true,
};

struct payload {
    int i;
    struct stack *s;
    struct grepper *g;
    uint32_t *sigs;
    const char *current_file;
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
            if (p->sigs[i]) {
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
    p->sigs[p->i] = 1;

    struct stat ss;
    lstat(name, &ss);

    size_t ln = strlen(name);
    if (S_ISDIR(ss.st_mode)) {
        DIR *dir = opendir(name);
        if (dir == NULL) {
            fprintf(stderr, "%s: ", name);
            perror("cannot open directory");
            goto CLEANUP;
        }
        struct dirent *dirent;
        while ((dirent = readdir(dir)) != NULL) {
            const char *dname = dirent->d_name;
            if (strcmp(dname, ".") == 0 || strcmp(dname, "..") == 0)
                continue;
            if (is_repo_bin(name, dname))
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
        int res = grepper_file(p->g, name, ss.st_size, p);
        if (res != 0)  {
            fprintf(stderr, "simdgrep: read %s: ", name);
            perror("");
        }
    }

CLEANUP:
    free(name);
    goto NEXT;
}

bool grep_callback(const struct grepline *l) {
    int64_t ln = (int64_t)(l->line_end - l->line_start);
    const char *name = ((struct payload *)l->memo)->current_file;
    if (l->is_binary) {
        printf("%s: binary file matches\n", name);
        return true;
    }
    if (ln < 1 << 20) {
        printf("%s:%lld:%.*s\n", name, l->nr, (int)ln, l->line_start);
    }
    return true;
}

int main(int argc, char **argv) 
{
    flags.verbose = true;
    flags.num_threads = sysconf(_SC_NPROCESSORS_ONLN);

    struct grepper g;
    g.file.after_lines = 0;
    g.file.callback = grep_callback;
    g.binary_mode = BINARY_IGNORE;
    g.use_regex = false;

    bool g_inited = false;
    const char *expr = "func\\("; // argv[1];
    g.rx_info = rx_extract_plain(expr);
    if (g.rx_info.unsupported_escape) {
        fprintf(stderr, "expression with unsupported escape at '%s'", g.rx_info.unsupported_escape);
        return 1;
    }
    for (int i = 0 ; i < g.rx_info.fixed_len; ) {
        int ln = strlen(g.rx_info.fixed_patterns + i);
        if (ln) {
            if (flags.verbose)
                printf("%sfixed pattern: %s\n", !g_inited && g.rx_info.fixed_start ? "start " : "",
                        g.rx_info.fixed_patterns + i);
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
        grepper_init(&g, "\n", flags.ignore_case);
    }
    if (!g.rx_info.pure) {
        g.use_regex = true;
        int rc = regcomp(&g.rx, expr, REG_EXTENDED | REG_ICASE);
        if (rc) {
            char buffer[1024];
            regerror(rc, &g.rx, buffer, 1024);
            fprintf(stderr, "invalid expression '%s': %s", expr, buffer);
            return 1;
        }
    }

    struct stack s;
    s.root = NULL;
    s.count = 0;

    char *startpoint = (char *)malloc(1000);
    strcpy(startpoint, "linux-6.13.2");
    stack_push(&s, startpoint);

    struct payload payloads[flags.num_threads];
    pthread_t threads[flags.num_threads];
    uint32_t *sigs = (uint32_t *)malloc(flags.num_threads * 4);
    for (int i = 0; i < flags.num_threads; ++i) {
        payloads[i].i = i;
        payloads[i].g = &g;
        payloads[i].s = &s;
        payloads[i].sigs = sigs;
        pthread_create(&threads[i], NULL, push, (void *)&payloads[i]);
    }
    for (int i = 0; i < flags.num_threads; ++i) {
        pthread_join(threads[i], NULL);
    }

    grepper_free(&g);
    free(sigs);
    int num_files = stack_free(&s);
    if (flags.verbose)
        printf("found %d files\n", num_files);
    return 0;
}
