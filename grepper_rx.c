#include "grepper.h"

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

struct rx_pattern_info rx_extract_plain(const char *s)
{
    int sl = strlen(s);
    struct rx_pattern_info info = {
        .fixed_patterns = (char *)malloc(sl + 1),
        .fixed_len = 0,
        .pure = true,
        .fixed_start = false,
        .unsupported_escape = NULL,
    };
    memset(info.fixed_patterns, 0, sl + 1);
    char *pp = info.fixed_patterns;
    int j = 0;
    for (int i = 0; i < sl; i++) {
        switch (s[i]) {
        case '|':
            info.pure = false;
            info.fixed_len = 0;
            return info;
        case '\\':
            if (++i >= sl) {
                info.unsupported_escape = s + i - 1;
                return info;
            }
            switch (s[i]) {
            case 'x':
                if (i + 2 >= sl || !isxdigit(*(s + i + 1)) || !isxdigit(*(s + i + 2))) {
                    info.unsupported_escape = s + i - 1;
                    return info;
                }
                char tmp[3] = {*(s + i + 1), *(s + i + 2), 0};
                pp[j++] = strtol(tmp, NULL, 16);
                i += 2;
                break;
            case 'r':
                pp[j++] = '\r'; break;
            case 't':
                pp[j++] = '\t'; break;
            case 'b': case 'B': case 'w': case 'W': case '<': case '>':
            case '`': case '\'':
                info.pure = false;
                pp[j++] = 0;
                break;
            case '(': case ')': case '[': case ']': case '+': case '*':
            case '?': case '|': case '.': case '^': case '$': case '\\':
            case '{': case '}':
                pp[j++] = s[i];
                break;
            default:
                char *end;
                strtol(s + i, &end, 10);
                if (end == s + i) {
                    info.unsupported_escape = s + i - 1;
                    return info;
                }
                info.pure = false;
                i = end - s;
            }
            break;
        case '.':
            pp[j++] = 0;
            info.pure = false;
            break;
        case '^': case '$': case '+': 
            info.pure = false;
            break;
        case '(':
            info.pure = false;
            pp[j++] = 0;
            i = _rx_skip_paren(s, i) - 1; // ++i
            break;
        case '{':
            info.pure = false;
            for (++i; s[i] != '}'; ++i);
            // fallthrough
        case '*': case '?': 
            info.pure = false;
            for (j--; j && ((uint8_t)pp[j] >> 6) == 2; j--);
            if (j)
                pp[j++] = 0;
            break;
        case '[':
            info.pure = false;
            pp[j++] = 0;
            i = _rx_skip_bracket(s, i) - 1; // ++i
            break;
        default:
            if (info.pure)
                info.fixed_start = true;
            pp[j++] = s[i];
        }
    }
    info.fixed_len = j;
    return info;
}
