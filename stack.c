#include "stack.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

void stack_push(uint8_t tid, struct stack *l, struct stacknode *n)
{
    uint64_t root = 0, zero = 0, mark = (uint64_t)tid << 56;
    // Use n->next high 8 bits as a mutex to prevent concurrent stack_push.
    if (!atomic_compare_exchange_strong(&n->next, &zero, mark)) {
        return;
	}

RETRY:
    root = atomic_load(&l->root);
    atomic_store(&n->next, root ? root : mark); // keep n->next not null, so locking preserves
	if (!atomic_compare_exchange_strong(&l->root, &root, mark | (uint64_t)n)) {
        goto RETRY;
	}
    atomic_fetch_add(&l->count, 1);
}

struct stacknode *stack_pop(uint8_t tid, struct stack *l)
{
    uint64_t mark = (uint64_t)tid << 56;
    while (1) {
        uint64_t root64 = atomic_load(&l->root);
        if ((root64 & STACK_NEXT) == 0)
            break;
        if (!atomic_compare_exchange_strong(&l->root, &root64, mark | (root64 & STACK_NEXT)))
            continue;
        root64 = mark | (root64 & STACK_NEXT);
        struct stacknode *root = (struct stacknode *)(root64 & STACK_NEXT);
        if (atomic_compare_exchange_strong(&l->root, &root64, root->next)) {
            atomic_fetch_add(&l->count, -1);
            atomic_store(&root->next, 0);
            return root;
        }
    }
	return NULL;
}

void strings_push(struct strings *ss, char *s)
{
    if (ss->len >= ss->cap) {
        size_t cap = (ss->cap + 1) * 2;
        char **grow = (char **)(realloc(ss->data, sizeof(char *) * cap));
        assert(grow);
        ss->data = grow;
        ss->cap = cap;
    }
    ss->data[ss->len++] = s;
}

void strings_clear(struct strings *ss)
{
    for (size_t i = 0; i < ss->len; i++)
        free(ss->data[i]);
    ss->len = 0;
}

void strings_free(struct strings *ss)
{
    for (size_t i = 0; i < ss->len; i++)
        free(ss->data[i]);
    if (ss->data)
        free(ss->data);
}
