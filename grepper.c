#include "grepper.h"
#include <stdlib.h>
#include <stdatomic.h>

static char bads[32] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

int64_t now() {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    return start.tv_sec * 1000000000L + start.tv_nsec;
}

#ifdef __cplusplus

#include <string>
#include <iostream>

#if defined(__x86_64__)
std::ostream& operator<<(std::ostream &out, const __m128i& v) {
    const uint8_t* p = (const uint8_t*)&v;
    out << '[';
    for (int i = 0; i < 16; i++) {
        out << (int)(*(p + i)) << (i == 15 ? ']' : ' ');
    }
    return out;
}
std::ostream& operator<<(std::ostream &out, const __m256i& v) {
    const uint8_t* p = (const uint8_t*)&v;
    out << '[';
    for (int i = 0; i < 32; i++) {
        out << (int)(*(p + i)) << (i == 31 ? ']' : ' ');
    }
    return out;
}
#endif 

#if defined(__aarch64__)
std::ostream& operator<<(std::ostream &out, const uint8x16_t& v) {
    const uint8_t* p = (const uint8_t*)&v;
    out << '[';
    for (int i = 0; i < 16; i++) {
        out << (int)(*(p + i)) << (i == 15 ? ']' : ' ');
    }
    return out;
}
std::ostream& operator<<(std::ostream &out, const uint16x8_t& v) {
    const uint16_t* p = (const uint16_t*)&v;
    out << '[';
    for (int i = 0; i < 8; i++) {
        out << (int)(*(p + i)) << (i == 7 ? ']' : ' ');
    }
    return out;
}
std::ostream& operator<<(std::ostream &out, const uint8x8_t& v) {
    const uint8_t* p = (const uint8_t*)&v;
    out << '[';
    for (int i = 0; i < 8; i++) {
        out << (int)(*(p + i)) << (i == 7 ? ']' : ' ');
    }
    return out;
}
#endif
#endif

#if defined(__x86_64__)

#include <emmintrin.h>
#include <immintrin.h>
#include <smmintrin.h>  

static int64_t countbyte(const char *s, const char *end, const uint8_t c)
{
    __m256i needle = _mm256_set1_epi8(c);
    int64_t count = 0;
    while (end - s >= 32) {
        __m256i haystack = _mm256_set_epi64x(*(uint64_t *)(s), *(uint64_t *)(s + 8), *(uint64_t *)(s + 16), *(uint64_t *)(s + 24));
        __m256i res = _mm256_cmpeq_epi8(haystack, needle);
        int map = _mm256_movemask_epi8(res);
        count += __builtin_popcount(map);
        s += 32;
    }
    __m128i needle2 = _mm_set1_epi8(c);
    while (end - s >= 16) {
        __m128i haystack = _mm_set_epi64x(*(uint64_t *)(s), *(uint64_t *)(s + 8));
        __m128i res = _mm_cmpeq_epi8(haystack, needle2);
        int map = _mm_movemask_epi8(res);
        count += __builtin_popcount(map);
        s += 16;
    }
    while (s < end) {
        if (*s++ == c) count++;
    }
    return count;
}

const char *index2bytes(const char *s, const char *end, uint16_t v, uint8_t mask, uint16_t mask2)
{
    __m256i needle = _mm256_set1_epi16(v);
    __m256i casemask = _mm256_set1_epi8(mask);
    while (s <= end - 17) {
        __m256i lo = _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)s));
        __m256i hi = _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)(s + 1)));
        __m256i haystack = _mm256_unpacklo_epi8(lo, hi);
        haystack = _mm256_insertf128_si256(haystack, _mm256_castsi256_si128(_mm256_unpackhi_epi8(lo, hi)), 1);
        haystack = _mm256_and_si256(haystack, casemask);
        __m256i res = _mm256_cmpeq_epi16(haystack, needle);
        uint32_t bitmap = _mm256_movemask_epi8(res);
        if (bitmap > 0) {
            int one = __builtin_ctz(bitmap);
            return s + one / 2;
        }
        s += 16;
    }
    for (; s < end - 1; ++s) {
        if ((*(uint16_t *)s & mask2) == v)
            return s;
    }
    return 0;
}

const char* indexcasebyte(const char *s, const char* end, const uint8_t lo, const uint8_t up)
{
    __m256i mask = _mm256_set_epi8(
            0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23,
            8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31
            );
    __m256i needle = _mm256_set1_epi16((uint16_t)lo << 8 | (uint16_t)up);
    while (s <= end - 16) {
        uint64_t v1 = *(uint64_t *)(s), v2 = *(uint64_t *)(s + 8);
        __m256i haystack = _mm256_shuffle_epi8(_mm256_set_epi64x(v1, v1, v2, v2), mask);
        __m256i res = _mm256_cmpeq_epi8(haystack, needle);
        uint32_t map = _mm256_movemask_epi8(res);
        if (map > 0) {
            int one = __builtin_clzll(map) - 32;
            return s + one / 2;
        }
        s += 16;
    }
    for (; s < end; ++s) {
        if (*s == lo || *s == up) return s;
    }
    return 0;
}

const char *indexbyte(const char *s, const char *end, const uint8_t a)
{
    __m256i needle = _mm256_set1_epi8(a);
    while (s <= end - 32) {
        __m256i haystack = _mm256_loadu_si256((__m256i *)s);
        __m256i res = _mm256_cmpeq_epi8(haystack, needle);
        uint32_t map = _mm256_movemask_epi8(res);
        if (map > 0) {
            int one = __builtin_ctzll(map);
            return s + one;
        }
        s += 32;
    }
    for (; s < end; ++s) {
        if (*s == a) return s;
    }
    return 0;
}

#elif defined(__aarch64__)

#include <arm_neon.h>

int64_t countbyte(const char *s, const char *end, uint8_t c) {
    uint8x16_t needle = vdupq_n_u8(c);
    int64_t count = 0;

    while (end - s >= 16) {
        uint8x16_t haystack = vld1q_u8((const uint8_t *)s);
        uint8x16_t res = vceqq_u8(haystack, needle);

        uint64_t mask1 = vgetq_lane_u64(vreinterpretq_u64_u8(res), 0);
        uint64_t mask2 = vgetq_lane_u64(vreinterpretq_u64_u8(res), 1);

        count += __builtin_popcountll(mask1);
        count += __builtin_popcountll(mask2);

        s += 16;
    }

    count /= 8;
    while (s < end) {
        if (*s++ == c) count++;
    }
    return count;
}

inline const char *index4bytes(const char *s, const char *end, uint32_t v, uint8_t mask, uint32_t mask4)
{
    uint32x4_t needle = vdupq_n_u32(v);
    uint8x16_t casemask = vdupq_n_u8(mask);

    while (s <= end - 16) {
        uint8x16_t haystack1 = vld1q_u8((const uint8_t *)s);
        uint8x16_t haystack2 = vextq_u8(haystack1, haystack1, 1);
        uint8x16_t haystack3 = vextq_u8(haystack1, haystack1, 2);
        uint8x16_t haystack4 = vextq_u8(haystack1, haystack1, 3);
        uint8x16_t zipped1 = vandq_u8(vzipq_u8(haystack1, haystack2).val[0], casemask);
        uint8x16_t zipped2 = vandq_u8(vzipq_u8(haystack3, haystack4).val[0], casemask);
        uint16x8_t zipped = vzipq_u16(vreinterpretq_u16_u8(zipped1), vreinterpretq_u16_u8(zipped2)).val[0];
        uint32x4_t interleaved = vreinterpretq_u32_u16(zipped);
        uint32x4_t mask = vceqq_u32(interleaved, needle);
        uint16x4_t res = vshrn_n_u32(mask, 1);
        uint64_t matches = vget_lane_u64(vreinterpret_u64_u16(res), 0);
        if (matches > 0) {
            int one = __builtin_ctzll(matches) / 16;
            return s + one;
        }
        s += 8;
    }

    while (s < end - 3) {
        if ((*(uint32_t *)s & mask4) == v)
            return s;
        s++;
    }
    return 0;
}

inline const char *index2bytes(const char *s, const char *end, uint16_t v, uint8_t mask, uint16_t mask2)
{
    uint16x8_t needle = vdupq_n_u16(v);
    uint8x16_t casemask = vdupq_n_u8(mask);

    while (s <= end - 16) {
        uint8x16_t haystack1 = vld1q_u8((const uint8_t *)s);
        uint8x16_t haystack2 = vextq_u8(haystack1, haystack1, 1);
        uint8x16_t zipped = vzipq_u8(haystack1, haystack2).val[0];
        zipped = vandq_u8(zipped, casemask);
        uint16x8_t interleaved = vreinterpretq_u16_u8(zipped);
        uint16x8_t mask = vceqq_u16(interleaved, needle);
        uint8x8_t res = vshrn_n_u16(mask, 1);
        uint64_t matches = vget_lane_u64(vreinterpret_u64_u8(res), 0);
        if (matches > 0) {
            int one = __builtin_ctzll(matches) / 8;
            return s + one;
        }
        s += 8;
    }

    while (s < end - 1) {
        if ((*(uint16_t *)s & mask2) == v)
            return s;
        s++;
    }
    return 0;
}

const char *indexcasebyte(const char *s, const char *end, const uint8_t lo, const uint8_t up)
{
    uint16x8_t needle = vdupq_n_u16((uint16_t)lo << 8 | (uint16_t)up);
    while (s <= end - 8) {
        uint8x16_t haystack1 = vcombine_u8(vmov_n_u64(*(const uint64_t *)(s)), vdup_n_u8(0));
        uint8x16_t interleaved = vzipq_u8(haystack1, haystack1).val[0];
        uint16x8_t mask = vreinterpretq_u16_u8(vceqq_u8(interleaved, needle));
        uint8x8_t res = vshrn_n_u16(mask, 4);
        uint64_t matches = vreinterpret_u64_u8(res);
        if (matches > 0) {
            int one = __builtin_ctzll(matches) >> 2;
            return s + one / 2;
        }
        s += 8;
    }

    for (; s < end; s++) {
        if (*s == lo || *s == up) return s;
    }
    return 0;
}

const char *indexbyte(const char *s, const char *end, const uint8_t a)
{
    uint8x16_t needle = vdupq_n_u8(a);
    uint8x16_t zeros = vdupq_n_u8(0);
    while (s <= end - 16) {
        uint8x16_t haystack = vld1q_u8((const uint8_t *)s);
        uint16x8_t mask = vreinterpretq_u16_u8(vceqq_u8(haystack, needle));
        uint8x8_t res = vshrn_n_u16(mask, 4);
        uint64_t m = vreinterpret_u64_u8(res);
        if (m > 0)
            return s + __builtin_ctzll(m) / 4;
        s += 16;
    }

    for (; s < end; s++) {
        if (*s == a) return s;
    }
    return 0;
}

#endif

void grepper_init(struct grepper *g, const char *find, bool ignore_case)
{
    g->ignore_case = ignore_case;
    g->lf = strlen(find);
    g->find = (char *)malloc(g->lf + 1);
    memcpy(g->find, find, g->lf + 1);
    g->lu = (char *)malloc(g->lf * 2);
    g->ll = g->lu + g->lf;
    for (int i = 0; i < g->lf; i++){
        if (ignore_case) {
            g->lu[i] = toupper(find[i]);
            g->ll[i] = tolower(find[i]);
        } else {
            g->lu[i] = g->ll[i] = find[i];
        }
    }
    memset(g->table, -1, 256 * sizeof(int));
    for (int i = 0; i < g->lf; i++) {
        g->table[(uint8_t)g->ll[i]] = i;
        g->table[(uint8_t)g->lu[i]] = i;
    }
    g->falses = 0;
    g->file.next_g = 0;
    g->_case_mask8 = ignore_case ? 0xDF : 0xFF;
    g->_case_mask16 = ignore_case ? 0xDFDF : 0xFFFF;
}

struct grepper *grepper_add(struct grepper *g, const char *find)
{
    struct grepper *ng = (struct grepper *)malloc(sizeof(struct grepper));
    grepper_init(ng, find, g->ignore_case);

    while (g->file.next_g)
        g = g->file.next_g;
    g->file.next_g = ng;

    return ng;
}

void grepper_free(struct grepper *g)
{
    free(g->find);
    free(g->lu);
    if (g->file.next_g)
        grepper_free(g->file.next_g);
    if (g->use_regex) {
        regfree(&g->rx);
    }
    free(g->rx_info.fixed_patterns);
}

static const char *indexcasestr_long(struct grepper *g, const char *s, const char *end)
{
    int64_t lf = g->lf;
    uint16_t v = *(uint16_t *)(g->find + lf - 2) & g->_case_mask16;
    while (s <= end - lf) {
        s = index2bytes(s + lf - 2, end, v, g->_case_mask8, g->_case_mask16);
        if (!s)
            return s;
        s -= lf - 2;
        int j = lf - 1;
        while (j >= 0 && (s[j] == g->ll[j] || s[j] == g->lu[j]))
            --j;
        if (j < 0)
            return s;
		int slide = j - g->table[(uint8_t)s[j]];
        s += slide < 1 ? 1 : slide;
    }
	return 0;
}

static const char *indexcasestr(struct grepper *g, const char *s, const char *end, comparer cmp)
{
	if (g->lf == 0 || s + g->lf > end)
        return 0;
    if (s + g->lf == end)
        return cmp(s, g->find, g->lf) == 0 ? s : 0;

    int64_t idx = 0;
    if (g->lf == 1)
        return g->ignore_case ?
            indexcasebyte(s, end, tolower(*g->find), toupper(*g->find)) :
            indexbyte(s, end, *g->find);

    // if (g->lf == 4) {
    //     uint32_t mask4 = (g->ignore_case ? 0xDFDFDFDF : 0xFFFFFFFF);
    //     uint32_t v = *(uint32_t *)g->find & mask4;
    //     while (s <= end - g->lf) {
    //         s = index4bytes(s, end, v, g->_case_mask8, mask4);
    //         if (!s)
    //             return s;
    //         if (strncasecmp(s, g->find, g->lf) == 0)
    //             return s;
    //         s++;
    //     }
    //     return NULL;
    // }

    if (g->lf >= MAX_BRUTE_FORCE_LENGTH)
        return indexcasestr_long(g, s, end);

    uint16_t v = *(uint16_t *)g->find & g->_case_mask16;
    while (s <= end - g->lf) {
        s = index2bytes(s, end, v, g->_case_mask8, g->_case_mask16);
        if (!s)
            return s;
        if (strncasecmp(s, g->find, g->lf) == 0)
            return s;
        s++;
    }
	return 0;
}

static const char *_grepper_find(struct grepper *g, const char *s, const char *end)
{
    return g->ignore_case ? 
        indexcasestr(g, s, end, strncasecmp) : indexcasestr(g, s, end, strncmp);
}

int64_t grepper_find(struct grepper *g, const char *s, int64_t ls)
{
    const char *found = _grepper_find(g, s, s + ls);
    return found ? found - s : -1;
}

int grepper_file(struct grepper *g, const char *path, int64_t size, void *memo)
{
    // const char *zzz = "0123456789abcde\x00 hij";
    // printf("start %s\n", path);
    // return 0;

    if (size == 0)
        return 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
 
    char *buf, *rx_tmp = NULL;
    bool readbuf = false;
    if (size < 1 << 16) {
        buf = (char *)malloc(size);
        if (read(fd, buf, size) == -1) {
            close(fd);
            return -1;
        }
        readbuf = true;
    } else {
        buf = (char *)mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
        if (buf == MAP_FAILED) {
            close(fd);
            return -1;
        }
    }

    // bool is_binary = !istext(buf, buf + (size > 1024 ? 1024 : size));
    bool is_binary = indexbyte(buf, buf + (size > 1024 ? 1024 : size), 0);
    if (is_binary && g->binary_mode == BINARY_IGNORE)
        goto CLEANUP;
 
    regmatch_t pmatch[1];
    int64_t nr = 0, rx_tmp_len = 0;
    const char *s = buf, *last_hit = buf, *end = buf + size;
    while (s < end) {
        s = _grepper_find(g, s, end);
        if (!s)
            break;
       
        const char *line_start = s;
        while (last_hit <= line_start - 1 && *(line_start - 1) != '\n')
            --line_start;
        const char *line_end = indexbyte(s, end, '\n');
        if (!line_end)
            line_end = end;

        nr += countbyte(last_hit, s, '\n');
        last_hit = s;

        struct grepper *ng = g->file.next_g;
        const char *ss = s + g->lf;
NG:
        if (ng) {
            ss = _grepper_find(ng, ss, line_end);
            if (!ss) {
                s += g->lf;
                continue;
            }
            ss = ss + ng->lf;
            ng = ng->file.next_g;
            goto NG;
        }

        if (g->use_regex) {
            const char *rx_start = g->rx_info.fixed_start ? s : line_start;
            int cap = line_end - rx_start + 1;
            if (cap > rx_tmp_len) {
                if (rx_tmp)
                    free(rx_tmp);
                rx_tmp = (char *)malloc(rx_tmp_len = cap);
            }
            memcpy(rx_tmp, rx_start, cap);
            rx_tmp[cap - 1] = 0;
            if (regexec(&g->rx, rx_tmp, 1, pmatch, 0) != 0) {
                s = line_end + 1;
                continue;
            }
        }

        struct grepline gl = {
            .memo = memo,
            .g = g,
            .nr = nr,
            .buf = buf,
            .is_binary = is_binary,
            .line_start = line_start,
            .line_end = line_end,
        };
        if (!g->file.callback(&gl))
            break;
        if (is_binary && g->binary_mode == BINARY)
            break;

        // After lines.
        // for (int a = 0; a < g->file.after_lines && line_end < end; a++) {
        //     line_start = line_end + 1;
        //     line_end = indexbyte(line_start, end, '\n');
        // }

        s = line_end;
    }
 
CLEANUP:
    readbuf ? free(buf) : munmap(buf, size);
    close(fd);
    if (rx_tmp)
        free(rx_tmp);
    return 0;
}
