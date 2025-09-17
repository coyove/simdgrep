#include "grepper.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MATCH_FILE   1
#define MATCH_SUFFIX_FILE   9
#define MATCH_FULL_FILE   2
#define MATCH_DIR    3
#define MATCH_SUFFIX_DIR 4
#define MATCH_FULL_DIR   5

static bool ends_with_rule(const char *a, const char *b)
{
    size_t al = strlen(a);
    size_t bl = (*(uint32_t *)b) & 0xFFFFFF;
    b += 4;
    return al >= bl && strcmp(a + al - bl, b) == 0;
}

const char *is_glob_path(const char *p, const char *end)
{
    const char *slash = NULL;
    for (const char *i = p; i < end; i++) {
        if (*i == '/') {
            slash = i;
        } else if ((*i == '*' || *i == '?' || *i == '[' || *i == ']') && (i == p || *(i - 1) != '\\')) {
            return slash ? slash + 1 : p;
        }
    }
    return NULL;
}

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

char *join_path(const char *root, const char *b, int len)
{
    char res[PATH_MAX];
    memset(res, 0, sizeof(res));
    /*
     * Windows style path "X:\abc", convert it to mingw (non-standard) style "/X/abc".
     * 'root' is always forward slash separated. (given .gitignores are all correctly written)
     * 
     */
    if (len >= 3 && isalpha(b[0]) && b[1] == ':' && b[2] == '\\') {
#ifdef __CYGWIN__ 
        memcpy(res, "/cygdrive/", 10);
        res[10] = tolower(b[0]);
        memcpy(res + 11, b + 2, len - 2 + 1);
#endif
#ifdef __MINGW32__ 
        res[0] = res[2] = '/';
        res[1] = tolower(b[0]);
        memcpy(res + 3, b + 3, len - 3);
#endif
        for (char *c = res; *c; c++) {
            *c = *c == '\\' ? '/' : *c;
        }
    } else if (len > 0 && b[0] == '/') {
        memcpy(res, b, len);
    } else {
        int ln = strlen(root);
        memcpy(res, root, ln);
        res[ln] = '/';
        memcpy(res + ln + 1, b, len);
    }
#ifdef _WIN32
    char *tmp = (char *)malloc(PATH_MAX);
    GetFullPathNameA(res, PATH_MAX, tmp, NULL);
    return tmp;
#else
    return realpath(res, NULL);
#endif
}

static bool _matcher_wildmatch(const char *pattern, const char *name, bool is_dir)
{
    unsigned flag = (*(uint32_t *)pattern) >> 24;
    if (flag == MATCH_SUFFIX_DIR)
        return is_dir && ends_with_rule(name, pattern);
    if (flag == MATCH_SUFFIX_FILE)
        return ends_with_rule(name, pattern);

    pattern += 4;
    if (flag == MATCH_FULL_FILE)
        return strcmp(name, pattern) == 0;
    if (flag == MATCH_FULL_DIR)
        return is_dir && strcmp(name, pattern) == 0;
    if (flag == MATCH_DIR)
        return is_dir && wildmatch(pattern, name, 0) == WM_MATCH;

    // if (flag == MATCH_FILE)
    return wildmatch(pattern, name, 0) == WM_MATCH;
}

static bool _matcher_negate_match(struct matcher *m, const char *name, bool is_dir)
{
    for (size_t i = 0; i < m->negate_excludes.len; i++) {
        if (_matcher_wildmatch(m->negate_excludes.data[i], name, is_dir))
            return true;
    }
    if (m->parent)
        return _matcher_negate_match(m->parent, name, is_dir);
    return false;
}

bool matcher_match(struct matcher *m, const char *name, bool is_dir, char *reason, int rn)
{
    const char *orig = name;
    if (m->root) {
        if (strstr(name, m->root) == name)
            name = name + strlen(m->root);
    }

    bool incl = is_dir || m->top->includes.len == 0;
    for (size_t i = 0; i < m->top->includes.len; i++) {
        if (_matcher_wildmatch(m->top->includes.data[i], name, is_dir)) {
            incl = true;
            break;
        }
    }
    if (!incl) {
        if (reason)
            DBG("[IGNORE] %s not included", name);
        return false;
    }

    for (size_t i = 0; i < m->excludes.len; i++) {
        const char *v = m->excludes.data[i];
        if (_matcher_wildmatch(v, name, is_dir)) {
            if (!_matcher_negate_match(m, name, is_dir)) {
                if (reason) {
                    char *tmp;
                    DBG("[IGNORE] %s by %s/%s, rule: %s", name, m->root, m->file, v);
                }
                return false;
            }
        }
    }
    return m->parent ? matcher_match(m->parent, orig, is_dir, reason, rn) : true;
}

void matcher_free(void *p)
{
    struct matcher *m = (struct matcher *)p;
    if (m->root)
        free(m->root);
    strings_free(&m->includes);
    strings_free(&m->excludes);
    strings_free(&m->negate_excludes);
    free(m);
}

bool matcher_add_rule(struct matcher *m, const char *l, const char *end, bool incl)
{
    struct strings *ss = &m->excludes;
    while (isspace(*(end - 1)))
        end--;

    if (end <= l || l[0] == '#')
        return false;

    if (l[0] == '!') {
        ss = &m->negate_excludes;
        if (++l >= end)
            return false;
    }

    if (*l == '/' && l + 1 == end)
        return false;

    bool simple = !is_glob_path(l, end);
    char *buf = (char *)malloc(end - l + 10);
    int off = 0;
    uint32_t *op = (uint32_t *)buf;
    if (*(end - 1) == '/') {
        if (simple)
            *op = *l == '/' ? MATCH_FULL_DIR : (off = 1, MATCH_SUFFIX_DIR);
        else 
            *op = MATCH_DIR;
        end--;
    } else if (*l == '/') {
        *op = simple ? MATCH_FULL_FILE : MATCH_FILE;
    } else {
        *op = simple ? (off = 1, MATCH_SUFFIX_FILE) : MATCH_FILE;
    }
    *op = (*op << 24) | (uint32_t)(end - l);
    if (off) 
        buf[4] = '/';
    memcpy(buf + 4 + off, l, end - l);
    buf[4 + off + end - l] = 0;
    strings_push(incl ? &m->includes : ss, buf);
    return true;
}

static struct matcher *_matcher_load_raw(char *dir, const char *f)
{
#ifndef _WIN32
    char *path = (char *)malloc(strlen(dir) + 1 + strlen(f) + 1);
    memcpy(path, dir, strlen(dir));
    memcpy(path + strlen(dir), "/", 1);
    memcpy(path + strlen(dir) + 1, f, strlen(f) + 1);

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

    if (m->excludes.len + m->negate_excludes.len > 0)
        return m;

    matcher_free(m);
#endif
    return NULL;
}

struct matcher *matcher_load_ignore_file(int dirfd, char *dir, struct matcher *parent, struct stack *matchers)
{
    struct matcher *m = NULL;
#ifndef _WIN32
    bool enter_repo = false;
    if (faccessat(dirfd, ".git", F_OK, AT_SYMLINK_NOFOLLOW) == 0) {
        enter_repo = true;
        m = _matcher_load_raw(dir, ".git/info/exclude");
        if (m) {
            m->parent = parent->top;
            m->top = parent->top;
            stack_push(0, matchers, (struct stacknode *)m);
            parent = m;
        }
    }

    if (faccessat(dirfd, ".gitignore", F_OK, AT_SYMLINK_NOFOLLOW) == 0) {
        m = _matcher_load_raw(dir, ".gitignore");
        if (m) {
            m->parent = enter_repo ? parent->top : parent;
            m->top = parent->top;
            stack_push(0, matchers, (struct stacknode *)m);
        }
    }
#endif
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

bool is_dir(const char *name, bool follow_link)
{
    struct stat ss;
#ifdef _WIN32
    stat(name, &ss);
#else
    follow_link ? stat(name, &ss) : lstat(name, &ss); 
#endif
    return S_ISDIR(ss.st_mode);
}
