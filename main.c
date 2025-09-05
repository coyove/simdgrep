#include "stack.h"
#include "grepper.h"
#include "wildmatch.h"
#include "pathutil.h"
#include "printer.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>

struct matcher *default_matcher;
struct stack tasks = {0};
struct stack matchers = {0};
struct grepper g = {0};

#ifdef SLOW_TASK
pthread_mutex_t task_lock = PTHREAD_MUTEX_INITIALIZER;
struct grepfile *taskq[100000];
static int taskqi = 0;
#endif

void push_task(uint8_t tid, struct grepfile *t)
{
#ifndef SLOW_TASK
    stack_push(tid, &tasks, (struct stacknode *)t);
#else
    pthread_mutex_lock(&task_lock);
    taskq[taskqi++] = t;
    pthread_mutex_unlock(&task_lock);
#endif
}

struct grepfile *pop_task(uint8_t tid)
{
#ifndef SLOW_TASK
    struct grepfile *file = (struct grepfile *)stack_pop(tid, &tasks);
#else
    pthread_mutex_lock(&task_lock);
    struct grepfile *file = 0;
    if (taskqi > 0)
        file = taskq[--taskqi];
    pthread_mutex_unlock(&task_lock);
#endif
    return file;
}

void new_task(uint8_t tid, char *name, struct matcher *m, bool is_dir)
{
    struct grepfile *file = (struct grepfile *)malloc(sizeof(struct grepfile));
    memset(file, 0, sizeof(struct grepfile));
    file->name = name;
    file->root_matcher = m;
    file->status = STATUS_QUEUED;
    if (is_dir)
        file->status |= STATUS_IS_DIR;
#ifdef __APPLE__
    file->lock = OS_UNFAIR_LOCK_INIT;
#else
    if (pthread_mutex_init(&file->lock, NULL) != 0) {
        ERR0("out of resource\n");
        exit(1);
    }
#endif
    push_task(tid, file);
    if (!is_dir)
        atomic_fetch_add(&flags.files, 1);
}

void *push(void *arg) 
{
    struct worker *p = (struct worker *)arg;
    struct grepfile *file;

NEXT:
    file = pop_task(p->tid);
    if (!file) {
        if (atomic_load(p->actives)) {
            struct timespec req = { .tv_sec = 0, .tv_nsec = 10000000 /* 10ms */ };
            nanosleep(&req, NULL);
            goto NEXT;
        }
        return 0;
    }
    atomic_fetch_add(p->actives, 1);

    if (file->status & STATUS_IS_DIR) {
        size_t ln = strlen(file->name);
        ln = file->name[ln - 1] == '/' ? ln - 1 : ln;
        if (!matcher_match(file->root_matcher, file->name, true, p->chunk.buf, p->chunk.buf_size)) {
            atomic_fetch_add(&flags.ignores, 1);
            DBG("ignore directory %s\n", p->chunk.buf);
            goto FREE_FILE;
        }

        DIR *dir = opendir(file->name);
        if (dir == NULL) {
            ERR("open %s", file->name);
            goto FREE_FILE;
        }

        struct matcher *root = file->root_matcher;
        if (!flags.no_ignore) {
            struct matcher *m = matcher_load_ignore_file(dirfd(dir), file->name, root, &matchers);
            if (m) {
                DBG("load ignore file from %s\n", m->root);
                root = m;
            }
        }

        struct dirent *dirent;
        while ((dirent = readdir(dir)) != NULL) {
            const char *dname = dirent->d_name;
            if (strcmp(dname, ".") == 0 || strcmp(dname, "..") == 0)
                continue;
            char *n = (char *)malloc(ln + 1 + strlen(dname) + 1);
            memcpy(n, file->name, ln);
            memcpy(n + ln, "/", 1);
            memcpy(n + ln + 1, dname, strlen(dname) + 1);
            bool dir = false;
            if (dirent->d_type == DT_LNK) {
                if (flags.no_symlink) {
                    free(n);
                    continue;
                }
                dir = is_dir(n, true);
            } else if (dirent->d_type == DT_UNKNOWN) {
                dir = is_dir(n, false);
            } else {
                dir = dirent->d_type == DT_DIR;
            }
            new_task(p->tid, n, root, dir);
        }
        closedir(dir);
    } else if (file->status & STATUS_OPENED) {
        int res = grepfile_inc_ref(file);
        if (res == INC_WAIT_FREEABLE) {
            push_task(p->tid, file);
            goto CONTINUE_LOOP;
        } else if (res == INC_FREEABLE) {
            goto FREE_FILE;
        } // INC_NEXT
        push_task(p->tid, file);
        res = grepfile_acquire_chunk(file, &p->chunk);
        if (res == FILL_OK || res == FILL_LAST_CHUNK) {
            grepfile_process_chunk(&g, &p->chunk);
        } else if (res == FILL_EOF) {
        } else {
            ERR("read %s", file->name);
        }
        grepfile_dec_ref(file);
        goto CONTINUE_LOOP;
    } else {
        int res = grepfile_open(&g, file, &p->chunk);
        if (res == OPEN_IGNORED) {
            atomic_fetch_add(&flags.ignores, 1);
            DBG("ignore file %s\n", p->chunk.buf);
        } else if (res == OPEN_BINARY_SKIPPED) {
            // Skip binary file. (-I flag)
        } else if (res == OPEN_EMPTY) {
            // Empty file.
        } else if (res == FILL_EOF) {
            __builtin_unreachable();
        } else if (res == FILL_LAST_CHUNK) {
            grepfile_process_chunk(&g, &p->chunk);
        } else if (res == FILL_OK) {
            while (file->status & STATUS_IS_BINARY_MATCHING || tasks.count > flags.num_threads) {
                grepfile_process_chunk(&g, &p->chunk);
                int res = grepfile_acquire_chunk(file, &p->chunk);
                if (res != FILL_OK && res != FILL_LAST_CHUNK) {
                    if (res != FILL_EOF)
                        ERR("read %s", file->name);
                    goto FREE_FILE;
                }
            }
            push_task(p->tid, file);
            grepfile_process_chunk(&g, &p->chunk);
            grepfile_dec_ref(file);
            goto CONTINUE_LOOP;
        } else {
            ERR("read %s", file->name);
        }
    }

FREE_FILE:
    grepfile_release(file);

CONTINUE_LOOP:
    atomic_fetch_sub(p->actives, 1);
    goto NEXT;
}

void usage()
{
    printf("usage: simdgrep [OPTIONS]... PATTERN FILES...\n");
    printf("PATTERN syntax is a dialect of POSIX extended regex\n");
    printf("FILE can contain glob patterns if you prefer simdgrep expanding it for you\n");
    printf("\te.g.: simdgrep PATTERN './dir/**/*.c'\n");
    printf("\n");
    printf("\t-F\tfixed string pattern\n");
    printf("\t-Z\tmatch case sensitively (insensitive by default)\n");
    printf("\t-P\tprint results without coloring\n");
    printf("\t-G\tsearch all files regardless of .gitignore\n");
    printf("\t-n\tignore symlinks\n");
    printf("\t-E\texclude files using glob patterns\n");
    printf("\t-q +/++\tsuppress warning/error messages\n");
    printf("\t   -/--\tenable log/debug messages\n");
    printf("\t-v N\toutput format bitmap (4: filename, 2: line number, 1: content)\n");
    printf("\t\te.g.: -v5 means printing filename and matched content\n");
    printf("\t-a\ttreat binary as text\n");
    printf("\t-I\tignore binary files\n");
    printf("\t-x N\ttruncate long matched lines to N bytes to make output compact\n");
    printf("\t-B N\tprint N lines of leading context\n");
    printf("\t-A N\tprint N lines of trailing context\n");
    printf("\t-C N\tprint N lines of output context\n");
    printf("\t-j N\tspawn N threads for searching (1-255)\n");
    exit(0);
}

int main(int argc, char **argv) 
{
    // return 0;
    if (pthread_mutex_init(&flags.lock, NULL) != 0) { 
        ERR0("simdgrep can't start");
        return 0; 
    } 
    flags.fixed_string = flags.no_ignore = false;
    flags.color = isatty(STDOUT_FILENO);
    flags.ignore_case = true;
    flags.quiet = 0;
    flags.xbytes = 1e8;
    flags.verbose = 7;
    flags.num_threads = MIN(255, sysconf(_SC_NPROCESSORS_ONLN) * 2);
    getcwd(flags.cwd, sizeof(flags.cwd));

    default_matcher = (struct matcher *)malloc(sizeof(struct matcher));
    memset(default_matcher, 0, sizeof(struct matcher));
    default_matcher->top = default_matcher;

    g.callback = print_callback;

    int cop;
    opterr = 0;
    while ((cop = getopt(argc, argv, "nhfFZPGaIA:B:C:E:q:x:j:v:z:")) != -1) {
        switch (cop) {
        case 'F': flags.fixed_string = true; break;
        case 'f': g.search_name = true; flags.verbose = 4; break;
        case 'Z': flags.ignore_case = false; break;
        case 'P': flags.color = false; break; 
        case 'q':
                  for (const char *q = optarg; *q; q++)
                      *q == '+' ? flags.quiet++ : flags.quiet--;
                  break;
        case 'G': flags.no_ignore = true; break;
        case 'n': flags.no_symlink = true; break;
        case 'a': g.binary_mode = BINARY_TEXT; break;
        case 'I': g.binary_mode = BINARY_IGNORE; break;
        case 'B': g.before_lines = MAX(0, atoi(optarg)); break;
        case 'A': g.after_lines = MAX(0, atoi(optarg)); break;
        case 'C': g.after_lines = g.before_lines = MAX(0, atoi(optarg)); break;
        case 'x': flags.xbytes = MAX(0, atoi(optarg)); break;
        case 'j': flags.num_threads = MIN(MAX(1, atoi(optarg)), 255); break;
        case 'v': flags.verbose = MIN(7, MAX(0, atoi(optarg))); break;
        case 'E':
                  LOG("* add exclude pattern %s\n", optarg);
                  matcher_add_rule(default_matcher, optarg, optarg + strlen(optarg), false);
                  break;
        default: usage();
        }
    }
    if (optind >= argc)
        usage();

    struct worker workers[flags.num_threads];
    int32_t _Atomic actives = 0;

    const char *expr = argv[optind];
    if (strlen(expr) == 0)
        goto EXIT;
    
    LOG("* search pattern: '%s', arg files: %d\n", expr, argc - optind - 1);
    LOG("* use %d threads\n", flags.num_threads);
    LOG("* binary mode: %d\n", g.binary_mode);
    LOG("* verbose mode: %d\n", flags.verbose);
    LOG("* print -%d+%d lines\n", g.before_lines, g.after_lines);
    LOG("* print line context %d bytes\n", flags.xbytes);

    if (flags.fixed_string) {
        int c = grepper_init(&g, expr, flags.ignore_case);
        if (c == INIT_INVALID_UTF8) {
            ERR0("invalid UTF8 string to search")
            goto EXIT;
        }
        if (c == INIT_OK) {
        } else if (flags.ignore_case) {
            WARN("special char case (0x%04x), conduct regex searching\n", c);
            flags.fixed_string = false;
        }
    }
    if (!flags.fixed_string) {
        grepper_init_rx(&g, expr, flags.ignore_case);
        if (g.rx.error) {
            ERR0("invalid expression: %s", g.rx.error);
            goto EXIT;
        }
        for (struct grepper *ng = &g; ng; ng = ng->next_g) {
            LOG("* fixed %s: %s\n", ng == &g && g.rx.fixed_start ? "prefix" : "pattern", ng->find);
        }
        if (g.slow_rx) {
            WARN("warning: no fixed pattern in '%s', regex searching will be extremely slow\n", expr);
        }
    }

    for (int arg_files = optind + 1; arg_files < argc; arg_files++) {
        const char *name = argv[arg_files];
        if (strlen(name) == 0)
            continue;
        const char *g = is_glob_path(name, name + strlen(name));
        char *joined;
        if (g) {
            matcher_add_rule(default_matcher, g, g + strlen(g), true);
            LOG("* add include pattern %s\n", g);
            joined = name == g ? join_path(flags.cwd, ".", 1) : join_path(flags.cwd, name, g - name);
        } else {
            joined = join_path(flags.cwd, name, strlen(name));
        }
        if (!joined) {
            ERR("can't resolve path %s", name);
            exit(1);
        }
        LOG("* search %s\n", joined);
        new_task(0, joined, default_matcher, is_dir(joined, true));
    }
    if (tasks.count == 0) {
        new_task(0, strdup(flags.cwd), default_matcher, true);
        LOG("* search current working directory\n");
    }
    for (int i = 0; i < flags.num_threads; ++i) {
        workers[i].actives = &actives;
        workers[i].tid = (uint8_t)i;
        memset(&workers[i].chunk, 0, sizeof(workers[i].chunk));
        workers[i].chunk.buf_size = DEFAULT_BUFFER_CAP;
        workers[i].chunk.cap_size = DEFAULT_BUFFER_CAP;
        workers[i].chunk.buf = (char *)malloc(65600);
        pthread_create(&workers[i].thread, NULL, push, (void *)&workers[i]);
    }
    for (int i = 0; i < flags.num_threads; ++i) {
        pthread_join(workers[i].thread, NULL);
        free(workers[i].chunk.buf);
    }
    assert(tasks.count == 0);

    LOG("* searched %lld files\n", flags.files);

    int num_ignorefiles = matchers.count;
    if (flags.no_ignore) {
        LOG("* all files are searched, no ignores\n");
    } else {
        LOG("* respected %d .gitignore, %lld files ignored\n", num_ignorefiles, flags.ignores);
    }
    stack_free(&matchers, matcher_free);

EXIT:
    grepper_free(&g);
    matcher_free(default_matcher);
    pthread_mutex_destroy(&flags.lock); 
    return 0;
}
