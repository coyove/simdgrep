#include "stack.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

const int N = 1000000;

struct payload {
    int i;
    struct stack *s;
    char *out;
};

void *push(void *arg) 
{
    struct payload *p = (struct payload *)arg;
    for (int n = 0; n < N; ++n) {
        int *c = (int *)malloc(sizeof(int));
        *c = p->i * N + n;
        stack_push(p->s, c);
    }
    int *o = (int *)malloc(sizeof(int));
    for (int c = 0; ; c++) {
        if (!stack_pop(p->s, (void **)&o)) {
            printf("%d %d\n", p->i, c);
            break;
        }
        p->out[*o] = 1;
        // printf("%d %d\n", p->i, *o);
    }
    return 0;
}

int main() 
{
    struct stack s;

    pthread_t threads[4];
    char *res = (char *)malloc(4 * N * sizeof(int));

    for (int i = 0; i < 4; ++i) {
        struct payload *p = (struct payload *)malloc(sizeof(struct payload));
        p->i = i;
        p->s = &s;
        p->out = res;
        pthread_create(&threads[i], NULL, push, (void *)p);
    }
    for (int i = 0; i < 4; ++i) {
        pthread_join(threads[i], NULL);
    }
    for (int i = 0; i < 4 * N; i++) {
        if (res[i] == 0) {
            perror("failed");
            return -1;
        }
    }

    printf("%d\n", stack_free(&s));
    return 0;
}
