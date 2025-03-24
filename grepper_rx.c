#include "grepper.h"

#include "STC/include/stc/csview.h"

#define i_import
#include "STC/include/stc/cregex.h"
#include "STC/include/stc/utf8.h"

#define RX_SOL 0x80000000
#define RX_EOL 0x40000000

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
                j += utf8_encode(pp + j, r);
                i = rend - s;
                break;
            case 'r': pp[j++] = '\r'; break;
            case 't': pp[j++] = '\t'; break;
            case 'v': pp[j++] = '\v'; break;
            case 'f': pp[j++] = '\f'; break;
            case 'a': pp[j++] = '\a'; break;
            case 's': case 'S': case 'w': case 'W': case 'd': case 'D':
                g->rx.use_regex++;
                pp[j++] = 0;
                break;
            case '(': case ')': case '[': case ']': case '+': case '*':
            case '?': case '|': case '.': case '^': case '$': case '\\':
            case '{': case '}':
                pp[j++] = s[i];
                break;
            default:
                g->rx.use_regex++;
                char *end;
                strtol(s + i, &end, 10);
                if (end == s + i)
                    goto UNSUPPORTED;
                i = end - s;
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
            if (j)
                pp[j++] = 0;
            break;
        case '[':
            g->rx.use_regex++;
            pp[j++] = 0;
            i = _rx_skip_bracket(s, i) - 1; // ++i
            break;
        default:
            if (!g->rx.use_regex)
                g->rx.fixed_start = true;
            pp[j++] = s[i];
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
    if (!g->find)
        grepper_init(g, "\n", false);

    free(pp);
    return;

UNSUPPORTED:
    free(pp);
    g->rx.error = (char *)malloc(256);
    snprintf(g->rx.error, 256, "invalid escape at '%s'", s + i - 1);
}

bool grepper_match(struct grepper *g, struct grepline *gl, csview *rx_match,
        const char *line_start, const char *s, const char *line_end)
{
    if (g->rx.use_regex) {
        const char *rx_start = g->rx.fixed_start ? s : line_start;
        char end = *line_end;

        // Create valid C string.
        *(char *)line_end = 0;
        int rc = cregex_match_sv(&g->rx.engine, csview_from_n(rx_start, line_end - rx_start), rx_match);
        *(char *)line_end = end;

        if (rc != CREG_OK)
            return false;

        gl->match_start = rx_match[0].buf - line_start;
        gl->match_end = gl->match_start + rx_match[0].size;
        return true;
    }
    if (g->rx.line_start && s != line_start)
        return false;
    if (g->rx.line_end && s + g->len != line_end)
        return false;
    return true;
}
