#ifndef _PATHUTIL_H
#define _PATHUTIL_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "stack.h"
#include "wildmatch.h"

typedef char *cct;
#define free_cct(p) free(*p)
#define i_key cct
#define i_no_clone
#define i_keydrop free_cct
#include "STC/include/stc/vec.h"

struct matcher {
    struct stacknode node;
    char *root;
    const char *file;
    vec_cct includes;
    vec_cct excludes;
    vec_cct negate_excludes;
    struct matcher *parent;
    struct matcher *top;
};

const char *is_glob_path(const char *p, const char *end);

const char *rel_path(const char *a, const char *b);

char *join_path(const char *cwd, const char *b, int len);

bool matcher_match(struct matcher *m, const char *name, bool is_dir, char *rule, int n);

void matcher_free(void *m);

bool matcher_add_rule(struct matcher *m, const char *l, const char *end, bool incl);

struct matcher *matcher_load_ignore_file(int dirfd, char *dir, struct matcher *parent, struct stack *matchers);

bool is_repo_bin(const char *dir, const char *name);

bool is_dir(const char *name, bool follow_link);

#endif
