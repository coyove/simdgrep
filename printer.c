#include "grepper.h"

struct _flags flags = {0};

static char printer_buffer[1 << 20];

static size_t printer_buffer_size = 0;

static void v_print(const char *data, ssize_t len)
{
    if (printer_buffer_size + len > sizeof(printer_buffer)) {
        fwrite(printer_buffer, 1, printer_buffer_size, stdout);
        fwrite(data, 1, len, stdout);
        printer_buffer_size = 0;
        return;
    }
    memcpy(printer_buffer + printer_buffer_size, data, len);
    printer_buffer_size += len;
}

static void print_n(const char *a, const char *b, int len, const char *c)
{
    if (a && *a)
        v_print(a, strlen(a));
    v_print(b, len);
    if (c && *c)
        v_print(c, strlen(c));
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

void print_flush()
{
    PRINT_LOCK();
    fwrite(printer_buffer, 1, printer_buffer_size, stdout);
    printer_buffer_size = 0;
    PRINT_UNLOCK();
}

bool print_callback(const struct grepline *l)
{
    const char *name = rel_path(flags.cwd, l->chunk->file->name);
    PRINT_LOCK();
    if ((l->chunk->file->status & STATUS_IS_BINARY) && l->g->binary_mode == BINARY) {
        if (flags.verbose && !l->is_ctxline) {
            printf(flags.verbose == 4 ? "%s\n" : "%s: binary file matches\n", name);
        }
        PRINT_UNLOCK();
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
    PRINT_UNLOCK();
    return flags.verbose != 4; // 4: filename only
}
