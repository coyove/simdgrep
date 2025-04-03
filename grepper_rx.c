#include "grepper.h"

#include "STC/include/stc/csview.h"

#define i_import
#include "STC/include/stc/cregex.h"
#include "STC/include/stc/utf8.h"

#define RX_SOL 0x80000000
#define RX_EOL 0x40000000

int torune(uint32_t *rune, const char *s)
{
    // for (int i=0; i < (int)(sizeof lowcase_ind/sizeof *lowcase_ind); ++i) {
    //     const struct CaseMapping entry = casemappings[lowcase_ind[i]];
    //     if (entry.m2 - entry.c2 != 32) {
    //         for (int c = entry.c1; c <= entry.c2; c++) 
    //             printf("\\u%04x", c);
    //     }
    // }
    // abort();
    return chartorune(rune, s);
}

static bool safecase(uint32_t r)
{
    char tmp[4];
    int lo = utf8_tolower(r), hi = utf8_toupper(r);
    int d = hi > lo ? hi - lo : lo - hi;
    return (d == 0 || d == 32) && utf8_encode(tmp, lo) == utf8_encode(tmp, hi);
}

const char *unsafecasestr(const char *s)
{
    while (*s) {
        uint32_t r;
        int n = torune(&r, s);
        if (!safecase(r))
            return s;
        s += n;
    }
    return NULL;
}

static int _rx_skip_bracket(const char *s, int i)
{
    int depth = 1;
    for (++i; depth; ++i) {
        char c = *(s + i);
        if (!c)
            return i;
        if (c != '[' && c != ']')
            continue;
        if (*(s + i - 1) == '\\')
            continue;
        if (c == '[')
            depth++;
        if (c == ']')
            depth--;
    }
    return i;
}

static int _rx_skip_paren(const char *s, int i)
{
    int depth = 1;
    for (++i; depth; ++i) {
        char c = *(s + i);
        if (!c)
            return i;
        if (c != '(' && c != ')' && c != '[')
            continue;
        if (*(s + i - 1) == '\\')
            continue;
        if (c == '[')
            i = _rx_skip_bracket(s, i) - 1; // ++i
        if (c == '(')
            depth++;
        if (c == ')')
            depth--;
    }
    return i;
}

void grepper_init_rx(struct grepper *g, const char *s, bool ignore_case)
{
    int i = 0, j = 0, sl = strlen(s);
    char *pp = (char *)malloc(sl + 1);
    memset(pp, 0, sl + 1);

    for (bool lit = false; i < sl; i++) {
        if (lit) {
            if (s[i] != '\\' || i + 1 >= sl || s[i + 1] != 'E') {
                pp[j++] = s[i];
                continue;
            }
        }
        switch (s[i]) {
        case '|':
            g->rx.use_regex++;
            j = 0;
            goto INIT;
        case '\\':
            if (++i >= sl)
                goto UNSUPPORTED;
            switch (s[i]) {
            case 'Q': lit = true; break;
            case 'E': lit = false; break;
            case 'x':
                if (i + 3 >= sl || s[i + 1] != '{')
                    goto UNSUPPORTED;
                char *rend;
                i += 2;
                int r = strtol(s + i, &rend, 16);
                if (*rend != '}' || r < 0 || r > 0x10FFFF)
                    goto UNSUPPORTED;
                i = rend - s;
                if (ignore_case && !safecase(r)) {
                    g->rx.use_regex++;
                    pp[j++] = 0;
                } else {
                    j += utf8_encode(pp + j, r);
                }
                break;
            case 'r': pp[j++] = '\r'; break;
            case 't': pp[j++] = '\t'; break;
            case 'v': pp[j++] = '\v'; break;
            case 'f': pp[j++] = '\f'; break;
            case 'a': pp[j++] = '\a'; break;
            case 's': case 'S': case 'w': case 'W': case 'd': case 'D':
            case 'b': case 'B': case 'A': case 'z': case 'Z': case 'p': 
            case 'P':
                g->rx.use_regex++;
                pp[j++] = 0;
                break;
            default:
                pp[j++] = s[i];
            }
            break;
        case '.':
            pp[j++] = 0;
            g->rx.use_regex++;
            break;
        case '^':
            g->rx.use_regex |= RX_SOL;
            break;
        case '$':
            g->rx.use_regex |= RX_EOL;
            break;
        case '+': 
            g->rx.use_regex++;
            break;
        case '(':
            g->rx.use_regex++;
            pp[j++] = 0;
            i = _rx_skip_paren(s, i) - 1; // ++i
            break;
        case '{':
            g->rx.use_regex++;
            for (++i; s[i] && s[i] != '}'; ++i);
            // fallthrough
        case '*': case '?': 
            g->rx.use_regex++;
            for (j--; j && ((uint8_t)pp[j] >> 6) == 2; j--);
            if (j < 0)
                goto UNSUPPORTED;
            if (j)
                pp[j++] = 0;
            break;
        case '[':
            g->rx.use_regex++;
            pp[j++] = 0;
            i = _rx_skip_bracket(s, i) - 1; // ++i
            break;
        default:
            if (ignore_case) {
                uint32_t r;
                int n = torune(&r, s + i);
                memcpy(pp + j, s + i, n);
                i += n - 1;
                if (!safecase(r)) {
                    g->rx.use_regex++;
                    pp[j++] = 0;
                    break;
                }
                j += n;
            } else {
                pp[j++] = s[i];
            }
            if (!g->rx.use_regex)
                g->rx.fixed_start = true;
        }
    }

INIT:
    if (g->rx.use_regex == RX_SOL) {
        // ^abc
        g->rx.use_regex = 0;
        g->rx.line_start = true;
    } else if (g->rx.use_regex == RX_EOL) {
        // abc$
        g->rx.use_regex = 0;
        g->rx.line_end = true;
    } else if (g->rx.use_regex == RX_SOL + RX_EOL) {
        // ^abc$
        g->rx.use_regex = 0;
        g->rx.line_start = g->rx.line_end = true;
    } else if (g->rx.use_regex) {
        g->rx.engine = cregex_make(s, ignore_case ? CREG_ICASE : 0);
        if (g->rx.engine.error != CREG_OK) {
            g->rx.use_regex = 0;
            g->rx.error = (char *)malloc(256);
            snprintf(g->rx.error, 256, "regexp error (%d)", g->rx.engine.error);
        } else {
            g->rx.groups = cregex_captures(&g->rx.engine) + 1;
        }
    }

    for (int i = 0 ; i < j; ) {
        int ln = strlen(pp + i);
        if (ln) {
            if (!g->find) {
                grepper_init(g, pp + i, ignore_case);
            } else {
                grepper_add(g, pp + i);
            }
        }
        i += ln + 1;
    }
    if (!g->find) {
        grepper_init(g, "<SLOWRX>", false);
        g->slow_rx = true;
    }

    free(pp);
    return;

UNSUPPORTED:
    free(pp);
    g->rx.error = (char *)malloc(256);
    snprintf(g->rx.error, 256, "invalid escape at '%s'", s + i - 1);
}

bool grepper_match(struct grepper *g, struct grepline *gl, struct linebuf *lb, csview *rx_match,
        const char *line_start, const char *s, const char *line_end)
{
    if (g->rx.use_regex) {
        const char *rx_start = g->rx.fixed_start ? s : line_start;
        char end_ch = *line_end;
        int rc;

        if (lb->is_mmap) {
            int64_t n = MIN(line_end - rx_start, lb->bufsize);
            memcpy(lb->_free, rx_start, n);
            lb->_free[n + 1] = 0;
            rc = cregex_match(&g->rx.engine, lb->_free, rx_match);
        } else {
            *(char *)line_end = 0;
            rc = cregex_match_sv(&g->rx.engine, csview_from_n(rx_start, line_end - rx_start), rx_match, CREG_DEFAULT);
            *(char *)line_end = end_ch;
        }

        if (rc != CREG_OK)
            return false;

        if (lb->is_mmap) {
            gl->match_start = rx_match[0].buf - lb->_free + (rx_start - line_start);
        } else {
            gl->match_start = rx_match[0].buf - line_start;
        }
        gl->match_end = gl->match_start + rx_match[0].size;
        return true;
    }
    if (g->rx.line_start && s != line_start)
        return false;
    if (g->rx.line_end && s + g->len != line_end)
        return false;
    return true;
}
