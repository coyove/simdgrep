#define __MAX_BRUTE_FORCE_LENGTH 20

#include "grepper.h"
#include <assert.h>

#ifdef __cplusplus
#include <string>
#include <iostream>
#endif

int64_t now() {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    return start.tv_sec * 1000000000L + start.tv_nsec;
}

int64_t test_grep(bool ignore_case, const char *s, const char *find) 
{
    struct grepper g;
    grepper_init(&g, find, ignore_case);
    int64_t r = grepper_find(&g, s, strlen(s));
    grepper_free(&g);
    return r;
}

int main()
{
    int seed = time(NULL);
    srand(seed);

    struct grepper g;
    grepper_init(&g, "func", true);
    g.exact_start = true;
    g.file.after_lines = 0;
    g.file.callback = [](const struct grepline *l) -> bool {
        std::string s(l->line_start, l->line_end);
        std::cout << l->nr << ' ' << s << "\n";
        return true;
    };
    // grepper_add(&g, "test");
    //
    FILE *fp = fopen("1.txt", "r");
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
    while ((read = getline(&line, &len, fp)) != -1) {
        line[strlen(line) - 1] = 0;
        grepper_file(&g, line, 0);
    }
    return 1;

    if (1) {
        assert(test_grep(1, "1", "1") == 0);
        assert(test_grep(1, "1", "0") == -1);
        assert(test_grep(1, "12", "12") == 0);
        assert(test_grep(1, "a2", "A") == 0);
        assert(test_grep(1, "a2", "2") == 1);
        assert(test_grep(1, "ab", "Ac") == -1);
        assert(test_grep(1, "ab", "Ab") == 0);
        assert(test_grep(0, "ab", "Ab") == -1);
        assert(test_grep(1, "6666660123456xy", "XY") == 13);
        assert(test_grep(1, "888888880123456xy", "XY") == 15);
        assert(test_grep(1, "0123456789abcdefg", "FG") == 15);
        assert(test_grep(1, "0123456789abcdefg_", "FG") == 15);
        assert(test_grep(1, "0123456789abcdefg1", "G1") == 16);
        assert(test_grep(1, "012345678xabcdefg1", "0123456789abcdefg1") == -1);
        assert(test_grep(1, "0123456789abcdefg1", "0123456789abcdefg1") == 0);
        assert(test_grep(1, "0123456789abcdefg10123456789abcdef", "fg") == 15);
        assert(test_grep(1, "0123456789abcdefg10123456789abcdef", "G101234") == 16);
        assert(test_grep(1, "中文0123456789ABCDEfg1", "0123456789abcdefg1") == 6);
        assert(test_grep(0, "中文0123456789ABCDEfg1", "0123456789abcdefg1") == -1);
        assert(test_grep(0, "中文0123456789ABCDabcd", "abcd") == 20);
        assert(test_grep(1, "0123456789abcdef01234", "0123456789abcdef01234") == 0);
        assert(test_grep(1, "0123456789abcdef01234", "012345678xabcdef01234") == -1);
#ifdef BIGTEST
        const int64_t N = ((int64_t)1 << 32) + 20;
        char * buf = (char *)malloc(N); // 3G
        memset(buf, ' ', N);
        memcpy(buf + N - 20, "012345678Xabcdef1234", 20);
        assert(test_grep(1, buf, "012345678xabcdef1234") == N - 20);
        assert(test_grep(1, buf, "x") == N - 20 + 9);
#endif
        const char *msg = "0123456789abcdefghijklmnopqrstuvwxyz";
        for (int i = 0; i < strlen(msg); i++) {
            assert(test_grep(1, msg, std::string(msg + i, 1).c_str()) == i);
        }
    }

    int c = 10000;
    char *s = (char *)malloc(c + 1);
    for (int i = 0 ;i < c; i++) {
        s[i] = rand() % 255 + 1;
        int flag = rand() % 4;
        if (flag == 0) {
            s[i] = 'a' + rand() % 26;
        } else if (flag == 1) {
            s[i] = 'A' + rand() % 26;
        } else if (flag == 2) {
            memcpy(s + i, std::string("中").c_str(), 3);
            i += 2;
        }
    }
    s[c] = 0;
    std::string find(s + 5003, rand() % 1000 + 100);
#ifndef exactcase
    for (int i = 0; i < find.size(); i++) {
        if ('a' <= find[i] && find[i] <= 'z') {
            find[i] = toupper(find[i]);
        }
    }
#endif

    // find = "中";
    if (true) {
        // FILE *f = fopen("testfile", "r");
        // fread(s, 1, c, f);
        // fclose(f);

        // char *nnn = (char *)malloc(430);
        // f = fopen("needle", "r");
        // fread(nnn, 1, 429, f);
        // fclose(f);
        // nnn[429] = 0;

        struct grepper g;
#ifdef exactcase
        grepper_init(&g, find.c_str(), false);
        std::cout << "exact case comparison\n";
#else
        grepper_init(&g, find.c_str(), true);
#endif

        // std::cout << std::hex << *(uint64_t *)(s + 5003) << '\n';
        // std::cout << std::hex << *(uint64_t *)(nnn) << '\n';

        int64_t zzz = 0, start_time = now();
        for (int i = 0; i < 1e6; i++) {
            s[100] = 'a' + i % 26;
            zzz += grepper_find(&g, s, c);
            if (zzz != 5003 - i) {
                // std::cout << i << '\n';
                // FILE *f = fopen("testfile", "w");
                // fwrite(s + 4988, 1, g.lf, f);
                // fclose(f);
                // f = fopen("needle", "w");
                // fwrite(g.find, 1, g.lf, f);
                // fclose(f);
            }
        }
        printf("%lld seed=%llu\n", zzz, seed);
        printf("finish in %ldns\n", now() - start_time);
    }
    if (1) {
        int64_t zzz = 0, start_time = now();
        for (int i = 0; i < 1e6; i++) {
            s[100] = 'a' + i % 26;
#ifdef exactcase
            zzz += strstr(s, find.c_str()) - s;
#else
            zzz += strcasestr(s, find.c_str()) - s;
#endif
        }
        printf("%lld\n", zzz);
        printf("finish in %ldns\n", now() - start_time);
    }
}
