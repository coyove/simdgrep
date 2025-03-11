#ifndef _PATHUTIL_H
#define _PATHUTIL_H

#include <string.h>
#include <limits.h>

#include "stack.h"

#define PATH_SEP '/'

struct matcher {
    char *root;
    struct stack includes;
    struct stack excludes;
    struct stack negate_excludes;
    struct matcher *parent;
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

bool matcher_match(struct matcher *m, const char *name, const char **rule, const char **source)
{
    const char *orig = name;
    if (m->root)
        name = rel_path(m->root, name);

    bool incl = m->includes.count == 0;
    for (struct stacknode *n = m->includes.root; n; n = n->next) {
        if (wildmatch(n->value, name, 0) == WM_MATCH) {
            incl = true;
            break;
        }
    }
    if (!incl) {
        *rule = "<include-rule>";
        *source = "<include-source>";
        return false;
    }

    for (struct stacknode *n = m->excludes.root; n && incl; n = n->next) {
        if (wildmatch(n->value, name, 0) == WM_MATCH) {
            for (struct stacknode *n = m->negate_excludes.root; n; n = n->next) {
                if (wildmatch(n->value, name, 0) == WM_MATCH)
                    goto NEGATED;
            }
            *rule = n->value;
            *source = m->root;
            return false;
NEGATED:
            incl = true;
        }
    }
    if (m->parent)
        return incl && matcher_match(m->parent, orig, rule, source);
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

struct matcher *matcher_load_ignore_file(char *dir)
{
    char *path = join_path(dir, ".gitignore");
    if (!path) 
        return NULL;

    struct matcher *m = (struct matcher *)malloc(sizeof(struct matcher));
    memset(m, 0, sizeof(struct matcher));

    m->root = path;

    char * line = NULL;
    size_t len = 0;
    ssize_t read;
    FILE * fp = fopen(path, "r");
    if (fp == NULL)
        return NULL;

    while ((read = getline(&line, &len, fp)) != -1) {
        if (read < 2 || line[0] == '#' || (line[0] == '!' && read == 2))
            continue;
        line[--read] = 0;
        char *buf = (char *)malloc(read + 10);
        bool negate = false;
        if (line[0] == '!') {
            memcpy(buf, line + 1, read);
            negate = true;
        } else {
            memcpy(buf, line, read + 1);
        }

        if (buf[strlen(buf) - 1] == '/')
            memcpy(buf + strlen(buf), "**", 3);

        if (buf[0] == '/')
            memcpy(buf, buf + 1, strlen(buf));

        if (negate) {
            stack_push(&m->negate_excludes, buf);
        } else {
            stack_push(&m->excludes, buf);
        }
    }

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
    const char *dot = strrchr(dir, '.');
    if (!dot)
        return false;
    if (strcmp(dot, ".git") == 0 && strcmp(name, "objects") == 0)
        return true;
    if (strcmp(dot, ".hg") == 0 && strcmp(name, "store") == 0)
        return true;
    return false;
}

#endif
