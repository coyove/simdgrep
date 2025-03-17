#ifndef _PATHUTIL_H
#define _PATHUTIL_H

#include <string.h>
#include <limits.h>

#include "stack.h"

#define PATH_SEP '/'

#define _INAME 1
#define _IPATH 2
#define _IDIR 3
#define _ISIMPLE 0x80

struct matcher {
    char *root;
    struct stack includes;
    struct stack excludes;
    struct stack negate_excludes;
    struct matcher *parent;
    struct matcher *top;
};

const char *rel_path(const char *a, const char *b)
{
    int al = strlen(a), bl = strlen(b);
    if (!al || !bl)
        return b;

    al -= a[al - 1] == PATH_SEP ? 1 : 0;
    bl -= b[bl - 1] == PATH_SEP ? 1 : 0;

    if (bl < al)
        return b;
    if (strncmp(a, b, al) != 0)
        return b;
    if (al == bl)
        return ".";
    return b + al + 1;
}

char *join_path(const char *cwd, const char *b)
{
    char res[PATH_MAX];
    if (strlen(b) > 0 && b[0] == PATH_SEP) {
        memcpy(res, b, strlen(b) + 1);
    } else {
        int ln = strlen(cwd);
        memcpy(res, cwd, ln);
        res[ln] = PATH_SEP;
        memcpy(res + ln + 1, b, strlen(b) + 1);
    }
    char *resolved = realpath(res, NULL);
    return resolved;
}

bool _matcher_wildmatch(const char *pattern, const char *name, bool is_dir)
{
    int flag = pattern[0] & 0x7F;
    if (flag == _IDIR) {
        if (!is_dir)
            return false;
        flag = _INAME;
    }
    if (flag == _INAME) {
        const char *s = strrchr(name, '/');
        name = s ? s + 1 : name;
    }
    if (pattern[0] & _ISIMPLE) {
        const char *f = strstr(name, pattern + 1);
        if (f == NULL)
            return false;
        if (f > name && *(f - 1) != '/')
            return false;
        f += strlen(pattern + 1);
        if (*(f + 1) != 0 && *(f + 1) != '/')
            return false;
        return true;
    }
    return wildmatch(pattern + 1, name, 0) == WM_MATCH;
}

bool _matcher_negate_match(struct matcher *m, const char *name, bool is_dir)
{
    for (struct stacknode *n = m->negate_excludes.root; n; n = n->next) {
        if (_matcher_wildmatch(n->value, name, is_dir))
            return true;
    }
    if (m->parent)
        return _matcher_negate_match(m->parent, name, is_dir);
    return false;
}

bool matcher_match(struct matcher *m, const char *name, bool is_dir, const char **rule, const char **source)
{
    const char *orig = name;
    if (m->root)
        name = rel_path(m->root, name);

    bool incl = is_dir || m->top->includes.count == 0;
    for (struct stacknode *n = m->top->includes.root; n; n = n->next) {
        if (_matcher_wildmatch(n->value, name, is_dir)) {
            incl = true;
            break;
        }
    }
    if (!incl) {
        *rule = *source = "<includes>";
        return false;
    }

    for (struct stacknode *n = m->excludes.root; n && incl; n = n->next) {
        if (_matcher_wildmatch(n->value, name, is_dir)) {
            if (!_matcher_negate_match(m, name, is_dir)) {
                *rule = n->value;
                *source = m->root;
                return false;
            }
        }
    }
    if (m->parent)
        return incl && matcher_match(m->parent, orig, is_dir, rule, source);
    return incl;
}

void matcher_free(struct matcher *m)
{
    if (m->root)
        free(m->root);
    for (struct stacknode *n = m->includes.root; n; n = n->next) free(n->value);
    for (struct stacknode *n = m->excludes.root; n; n = n->next) free(n->value);
    for (struct stacknode *n = m->negate_excludes.root; n; n = n->next) free(n->value);
    stack_free(&m->includes);
    stack_free(&m->excludes);
    stack_free(&m->negate_excludes);
}

bool matcher_add_rule(struct matcher *m, const char *l, const char *end, bool incl)
{
    struct stack *ss = &m->excludes;
    char *buf = (char *)malloc(end - l + 10);
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

struct matcher *matcher_load_ignore_file(char *dir)
{
    char *path = join_path(dir, ".gitignore");
    if (!path) 
        return NULL;

    struct matcher *m = (struct matcher *)malloc(sizeof(struct matcher));
    memset(m, 0, sizeof(struct matcher));

    m->root = path;

    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return NULL;

    while ((read = getline(&line, &len, fp)) > 0)
        matcher_add_rule(m, line, line + read, false);

    fclose(fp);
    if (line)
        free(line);
    if (m->excludes.count + m->negate_excludes.count == 0) {
        matcher_free(m);
        return NULL;
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


const char *matcher_explain_rule(char flag)
{
    switch (flag & 0x7F) {
    case _IDIR:
        return "DIR";
    case _INAME:
        return "NAME";
    default:
        return "GLOB";
    }
}

#endif
