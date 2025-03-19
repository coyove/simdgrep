#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>

#include "pathutil.h"

#define _INAME 'N'
#define _IPATH 'G'
#define _IDIR 'D'
#define _ISIMPLE 0x80

const char *rel_path(const char *a, const char *b)
{
    int al = strlen(a), bl = strlen(b);
    if (!al || !bl)
        return b;

    al -= a[al - 1] == '/' ? 1 : 0;
    bl -= b[bl - 1] == '/' ? 1 : 0;

    if (bl < al)
        return b;
    if (strncmp(a, b, al) != 0)
        return b;
    if (al == bl)
        return ".";
    return b + al + 1;
}

char *join_path(const char *root, const char *b)
{
    char res[PATH_MAX];
#ifdef __MINGW32__
    /*
     * Windows style path "X:\abc", convert it to mingw (non-standard) style "/X/abc".
     * 'root' is always forward slash separated. (given .gitignores are all correctly written)
     * 
     */
    if (strlen(b) >= 3 && isalpha(b[0]) && b[1] == ':' && b[2] == '\\') {
        res[0] = res[2] = '/';
        res[1] = tolower(b[0]);
        memcpy(res + 3, b + 3, strlen(b) + 1 - 3);
        for (char *c = res; *c; c++) {
            *c = *c == '\\' ? '/' : *c;
        }
    } else 
#endif
    if (strlen(b) > 0 && b[0] == '/') {
        memcpy(res, b, strlen(b) + 1);
    } else {
        int ln = strlen(root);
        memcpy(res, root, ln);
        res[ln] = '/';
        memcpy(res + ln + 1, b, strlen(b) + 1);
    }
    return realpath(res, NULL);
}

static bool _matcher_wildmatch(const char *pattern, const char *name, bool is_dir)
{
    int simple = pattern[0] & _ISIMPLE, flag = pattern[0] & 0x7F;
    if (flag == _IDIR) {
        if (!is_dir)
            return false;
        flag = _INAME;
    }
    if (flag == _INAME) {
        const char *s = strrchr(name, '/');
        name = s ? s + 1 : name;
        if (simple)
            return strcmp(name, pattern + 1) == 0;
    }
    pattern++;
    if (simple) {
        const char *f = strstr(name, pattern);
        if (f == NULL)
            return false;
        if (f > name && *(f - 1) != '/')
            return false;
        f += strlen(pattern);
        return *f == 0 || *f == '/';
    }
    return wildmatch(pattern, name, 0) == WM_MATCH;
}

static bool _matcher_negate_match(struct matcher *m, const char *name, bool is_dir)
{
    FOREACH(&m->negate_excludes, n) {
        if (_matcher_wildmatch(n->value, name, is_dir))
            return true;
    }
    if (m->parent)
        return _matcher_negate_match(m->parent, name, is_dir);
    return false;
}

bool matcher_match(struct matcher *m, const char *name, bool is_dir, char *reason, int rn)
{
    const char *orig = name;
    if (m->root)
        name = rel_path(m->root, name);

    bool incl = is_dir || m->top->includes.count == 0;
    FOREACH(&m->top->includes, n) {
        if (_matcher_wildmatch(n->value, name, is_dir)) {
            incl = true;
            break;
        }
    }
    if (!incl) {
        if (reason)
            snprintf(reason, rn, "%s -> (I)", name);
        return false;
    }

    FOREACH(&m->excludes, n) {
        if (_matcher_wildmatch(n->value, name, is_dir)) {
            if (!_matcher_negate_match(m, name, is_dir)) {
                if (reason) {
                    char *v = n->value;
                    snprintf(reason, rn, "%s -> %s/%s -> (%c)%s",
                            name, m->root, m->file, v[0] & 0x7F, v + 1);
                }
                return false;
            }
        }
    }
    return m->parent ? matcher_match(m->parent, orig, is_dir, reason, rn) : true;
}

void matcher_free(struct matcher *m)
{
    if (m->root)
        free(m->root);
    FOREACH(&m->includes, n)
        free(n->value);
    FOREACH(&m->excludes, n)
        free(n->value);
    FOREACH(&m->negate_excludes, n)
        free(n->value);
    stack_free(&m->includes);
    stack_free(&m->excludes);
    stack_free(&m->negate_excludes);
}

bool matcher_add_rule(struct matcher *m, const char *l, const char *end, bool incl)
{
    struct stack *ss = &m->excludes;
    while (*(end - 1) == ' ' || *(end - 1) == '\r' || *(end - 1) == '\n')
        end--;

    if (end <= l || l[0] == '#')
        return false;

    if (l[0] == '!') {
        ss = &m->negate_excludes;
        if (++l >= end)
            return false;
    }

    if (l[0] == '/' && ++l >= end)
        return false;

    int simple = _ISIMPLE;
    for (const char *i = l; i < end; i++) {
        if ((*i == '*' || *i == '?' || *i == '[' || *i == ']') && (i == l || *(i - 1) != '\\')) {
            simple = 0;
            break;
        }
    }
    char *buf = (char *)malloc(end - l + 10);
    const char *slash = strrchr(l, '/');
    if (*(end - 1) == '/' && slash == end - 1) {
        buf[0] = simple | _IDIR; // 'abc/': match directory only
    } else if (!slash) {
        buf[0] = simple | _INAME; // 'abc/def': match name
    } else {
        buf[0] = simple | _IPATH; // 'abc/**/def': match any path
    }
    memcpy(buf + 1, l, end - l);
    buf[1 + end - l] = 0;
    stack_push(incl ? &m->includes : ss, buf);
    return true;
}

static struct matcher *_matcher_load_raw(char *dir, const char *f)
{
    char *path = join_path(dir, f);
    if (!path) 
        return NULL;

    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        free(path);
        return NULL;
    }

    struct matcher *m = (struct matcher *)malloc(sizeof(struct matcher));
    memset(m, 0, sizeof(struct matcher));

    while ((read = getline(&line, &len, fp)) > 0)
        matcher_add_rule(m, line, line + read, false);

    fclose(fp);
    if (line)
        free(line);

    path[strlen(dir)] = 0;
    m->root = path;
    m->file = f;

    if (m->excludes.count + m->negate_excludes.count == 0) {
        matcher_free(m);
        return NULL;
    }
    return m;
}

struct matcher *matcher_load_ignore_file(char *dir, struct matcher *parent, struct stack *matchers)
{
    struct matcher *m = _matcher_load_raw(dir, ".git/info/exclude");
    if (m) {
        m->parent = parent;
        m->top = parent->top;
        stack_push(matchers, m);
        parent = m;
    }
    m = _matcher_load_raw(dir, ".gitignore");
    if (m) {
        m->parent = parent;
        m->top = parent->top;
        stack_push(matchers, m);
    }
    return m;
}

bool is_repo_bin(const char *dir, const char *name)
{
    return false;
    const char *dot = strrchr(dir, '.');
    if (!dot)
        return false;
    if (strcmp(dot, ".git") == 0 && strcmp(name, "objects") == 0)
        return true;
    if (strcmp(dot, ".hg") == 0 && strcmp(name, "store") == 0)
        return true;
    return false;
}
