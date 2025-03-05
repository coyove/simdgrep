#ifndef _HEADER_S
#define _HEADER_S

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

struct stacknode {
    void *value;
    _Atomic int32_t removed;
    struct stacknode * _Atomic next;
    struct stacknode * _Atomic next2;
};

struct stack {
    _Atomic int64_t count;
    struct stacknode * _Atomic root;
};

void stack_push(struct stack *l, void *v);

bool stack_pop(struct stack *l, void **v);

int stack_free(struct stack *l);

void stack_print(struct stack *l);

#endif

