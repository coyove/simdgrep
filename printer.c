#include "printer.h"

struct _flags flags = {0};

static void print_n(const char *a, const char *b, int len, const char *c)
{
    if (a && *a)
        fwrite(a, 1, strlen(a), stdout);
    fwrite(b, 1, len, stdout);
    if (c && *c)
        fwrite(c, 1, strlen(c), stdout);
}

static void print_s(const char *a, const char *b, const char *c)
{
    print_n(a, b, strlen(b), c);
}

static void print_i(const char *a, uint64_t b, const char *c)
{
    char buf[32];
    char *s = buf + 32;
    do {
        *(--s) = b % 10 + '0';
        b /= 10;
    } while(b);
    print_n(a, s, buf + 32 - s, c);
}

bool print_callback(const struct grepline *l)
{
    const char *name = rel_path(flags.cwd, l->file->file->name);
    LOCK();
    if (l->file->file->status & STATUS_IS_BINARY_MATCHING) {
        if (flags.verbose && !l->is_ctxline) {
            printf(flags.verbose == 4 ? "%s\n" : "%s: binary file matches\n", name);
        }
        UNLOCK();
        return false;
    } else {
        int i = 0, j = l->len;
        if (l->len > flags.xbytes + l->g->len + 10) {
            i = MAX(0, l->match_start - flags.xbytes / 2);
            j = MIN(l->len, l->match_end + flags.xbytes / 2);
            for (; i > 0 && ((uint8_t)l->line[i] >> 6) == 2; i--);
            for (; j < l->len && ((uint8_t)l->line[j] >> 6) == 2; j++);
        }

        if (l->is_ctxline || !flags.color) {
            if (flags.verbose & 4)
                print_s(0, name, flags.verbose > 4 ? ":" : "");
            if (flags.verbose & 2)
                print_i(0, l->nr + 1, (flags.verbose & 1) ? ":" : "");
            if (flags.verbose & 1) {
                print_s(0, i == 0 ? "" : "...", 0);
                print_n(0, l->line + i, j - i, 0);
                print_s(0, j == l->len ? "" : "...", 0);
            }
        } else {
            if (flags.verbose & 4)
                print_s("\033[0;35m", name, flags.verbose > 4 ? "\033[0m:" : "\033[0m"); 
            if (flags.verbose & 2)
                print_i("\033[1;32m", l->nr + 1, (flags.verbose & 1) ? "\033[0m:" : "\033[0m"); 
            if (flags.verbose & 1) {
                // left ellipsis
                print_s("\033[1;33m", i == 0 ? "" : "...", "\033[0m"); 
                // before context
                print_n(0, l->line + i, l->match_start - i, 0); 
                // hightlight match
                print_n("\033[1;31m", l->line + l->match_start, l->match_end - l->match_start, "\033[0m"); 
                // after context
                print_n(0, l->line + l->match_end, j - l->match_end, 0); 
                // right ellipsis
                print_s("\033[1;33m", j == l->len ? "" : "...", "\033[0m"); 
            }
        }
        if (flags.verbose)
            print_s(0, "\n", 0);
    }
    UNLOCK();
    return flags.verbose != 4; // 4: filename only
}
