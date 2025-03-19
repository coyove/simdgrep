#ifndef _PATHUTIL_H
#define _PATHUTIL_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "stack.h"
#include "wildmatch.h"

struct matcher {
    char *root;
    const char *file;
    struct stack includes;
    struct stack excludes;
    struct stack negate_excludes;
    struct matcher *parent;
    struct matcher *top;
};

const char *rel_path(const char *a, const char *b);

char *join_path(const char *cwd, const char *b);

bool matcher_match(struct matcher *m, const char *name, bool is_dir, char *rule, int n);

void matcher_free(struct matcher *m);

bool matcher_add_rule(struct matcher *m, const char *l, const char *end, bool incl);

struct matcher *matcher_load_ignore_file(char *dir, struct matcher *parent, struct stack *matchers);

bool is_repo_bin(const char *dir, const char *name);

#endif
