#include "grepper.h"
#include <stdlib.h>
#include <stdatomic.h>

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

int64_t countbyte(const char *s, const char *end, const uint8_t c)
{
    __m256i needle = _mm256_set1_epi8(c);
    int64_t count = 0;
    while (end - s >= 32) {
        __m256i haystack = _mm256_set_epi64x(*(uint64_t *)(s), *(uint64_t *)(s + 8), *(uint64_t *)(s + 16), *(uint64_t *)(s + 24));
        __m256i res = _mm256_cmpeq_epi8(haystack, needle);
        uint32_t map = _mm256_movemask_epi8(res);
        count += __builtin_popcountll(map);
        s += 32;
    }
    __m128i needle2 = _mm_set1_epi8(c);
    while (end - s >= 16) {
        __m128i haystack = _mm_set_epi64x(*(uint64_t *)(s), *(uint64_t *)(s + 8));
        __m128i res = _mm_cmpeq_epi8(haystack, needle2);
        uint16_t map = _mm_movemask_epi8(res);
        count += __builtin_popcountll(map);
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

const char *indexlastbyte(const char *start, const char *s, const uint8_t a)
{
    __m256i needle = _mm256_set1_epi8(a);
    while (start <= s - 32) {
        s -= 32;
        __m256i haystack = _mm256_loadu_si256((__m256i *)s);
        __m256i res = _mm256_cmpeq_epi8(haystack, needle);
        uint32_t map = _mm256_movemask_epi8(res);
        if (map > 0) {
            int one = __builtin_clzll(map) - 32;
            return s + 31 - one;
        }
    }
    for (s--; s >= start; s--) {
        if (*s == a) return s;
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
    uint8x8_t lookup1 = {0, 1, 2, 3, 1, 2, 3, 4}; 
    uint8x8_t lookup2 = {2, 3, 4, 5, 3, 4, 5, 6};
    while (s <= end - 16) {
        uint8x16_t haystack1 = vld1q_u8((const uint8_t *)s);
        uint8x8x2_t table = {vget_low_u8(haystack1)};
        uint8x16_t interleaved = vcombine_u8(vtbl2_u8(table, lookup1), vtbl2_u8(table, lookup2));
        uint32x4_t mask = vceqq_u32(vreinterpretq_u32_u8(interleaved), needle);
        uint16x4_t res = vshrn_n_u32(mask, 1);
        uint64_t matches = vget_lane_u64(vreinterpret_u64_u16(res), 0);
        if (matches > 0) {
            int one = __builtin_ctzll(matches) / 16;
            return s + one;
        }
        s += 4;
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

const char *indexlastbyte(const char *start, const char *s, const uint8_t a)
{
    uint8x16_t needle = vdupq_n_u8(a);
    while (start <= s - 16) {
        s -= 16;
        uint8x16_t haystack = vld1q_u8((const uint8_t *)s);
        uint16x8_t mask = vreinterpretq_u16_u8(vceqq_u8(haystack, needle));
        uint8x8_t res = vshrn_n_u16(mask, 4);
        uint64_t m = vreinterpret_u64_u8(res);
        if (m > 0) {
            return s + 15 - __builtin_clzll(m) / 4;
        }
    }

    for (s--; s >= start; s--) {
        if (*s == a)
            return s;
    }
    return 0;
}

#endif

void grepper_init(struct grepper *g, const char *find, bool ignore_case)
{
    g->ignore_case = ignore_case;
    g->len = strlen(find);
    g->find = (char *)malloc(g->len * 3);
    g->findupper = g->find + g->len;
    g->findlower = g->findupper + g->len;
    memset(g->table, -1, 256 * sizeof(int));
    for (int i = 0; i < g->len; i++){
        if (ignore_case) {
            g->findupper[i] = toupper(find[i]);
            g->findlower[i] = tolower(find[i]);
        } else {
            g->findupper[i] = g->findlower[i] = find[i];
        }
        g->find[i] = find[i];
        g->table[(uint8_t)g->findlower[i]] = g->table[(uint8_t)g->findupper[i]] = i;
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
    if (g->file.next_g)
        grepper_free(g->file.next_g);
    if (g->use_regex)
        regfree(&g->rx);
    free(g->rx_info.fixed_patterns);
}

static const char *indexcasestr_long(struct grepper *g, const char *s, const char *end)
{
    int64_t lf = g->len;
    uint16_t v = *(uint16_t *)(g->find + lf - 2) & g->_case_mask16;
    while (s <= end - lf) {
        s = index2bytes(s + lf - 2, end, v, g->_case_mask8, g->_case_mask16);
        if (!s)
            return s;
        s -= lf - 2;
        int j = lf - 1;
        while (j >= 0 && (s[j] == g->findlower[j] || s[j] == g->findupper[j]))
            --j;
        if (j < 0)
            return s;
		s += MAX(1, j - g->table[s[j]]);
    }
	return 0;
}

static const char *indexcasestr(struct grepper *g, const char *s, const char *end)
{
	if (s + g->len > end)
        return 0;

    if (s + g->len == end) {
        if (g->ignore_case)
            return strncasecmp(s, g->find, g->len) == 0 ? s : 0;
        return strncmp(s, g->find, g->len) == 0 ? s : 0;
    }

    if (g->len == 1) {
        if (g->find[0] == '\n')
            return s;
        return g->ignore_case ?
            indexcasebyte(s, end, tolower(*g->find), toupper(*g->find)) :
            indexbyte(s, end, *g->find);
    }

    if (1 || g->len >= MAX_BRUTE_FORCE_LENGTH)
        return indexcasestr_long(g, s, end);

    uint16_t v = *(uint16_t *)g->find & g->_case_mask16;
    int adv = (v >> 8) == (v & 0xFF) ? 1 : 2;
    while (s <= end - g->len) {
        s = index2bytes(s, end, v, g->_case_mask8, g->_case_mask16);
        if (!s)
            return s;
        for (int i = 0; i < g->len; i++) {
            if (*(s + i) != g->findupper[i] && *(s + i) != g->findlower[i])
                goto FALSES;
        }
        return s;
FALSES:
        atomic_fetch_add(&g->falses, 1);
        s += adv;
    }
	return 0;
}

int64_t grepper_find(struct grepper *g, const char *s, int64_t ls)
{
    const char *found = indexcasestr(g, s, s + ls);
    return found ? found - s : -1;
}

int grepper_file(struct grepper *g, const char *path, int64_t size, struct grepper_ctx *ctx)
{
    if (size == 0)
        return 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    struct linebuf *lb = &ctx->lbuf;
    lb->fd = fd;
    lb->overflowed = false;
    lb->lines = lb->len = lb->datalen = 0;
    buffer_fill(lb, path);

    lb->is_binary = indexbyte(lb->buffer, lb->buffer + (lb->len > 1024 ? 1024 : lb->len), 0);
    if (lb->is_binary && g->binary_mode == BINARY_IGNORE)
        goto CLEANUP;
 
    regmatch_t pmatch[1];
    int remain_after_lines = 0;
    while (lb->len) {
        struct grepline gl = {
            .ctx = ctx,
            .g = g,
            .nr = lb->lines,
            .is_afterline = false,
        };
        const char *s = lb->buffer, *last_hit = lb->buffer, *end = lb->buffer + lb->len;

        // Process the remaining afterlines from the prev line buffer.
        for (int i = 0; remain_after_lines > 0 && s < end; i++,remain_after_lines--) {
            const char *line_end = indexbyte(s, end, '\n');
            if (!line_end)
                line_end = end;
            gl.line = s;
            gl.match_start = gl.match_end = gl.len = line_end - s;
            gl.is_afterline = true;
            if (!g->file.callback(&gl))
                goto CLEANUP;
            last_hit = s = line_end + 1;
            gl.nr++;
        }

        while (s < end) {
            s = indexcasestr(g, s, end);
            if (!s)
                break;

            const char *line_start = indexlastbyte(lb->buffer, s, '\n');
            line_start = line_start ? line_start + 1 : lb->buffer;

            const char *line_end = indexbyte(s, end, '\n');
            line_end = line_end ? line_end : end;

            gl.nr += countbyte(last_hit, s, '\n');
            last_hit = s;

            gl.line = line_start;
            gl.len = line_end - line_start;
            gl.match_start = s - line_start;
            gl.match_end = s + g->len - line_start;
            gl.is_afterline = false;

            struct grepper *ng = g->file.next_g;
            const char *ss = s + g->len;
NG:
            if (ng) {
                ss = indexcasestr(ng, ss, line_end);
                if (!ss) {
                    s += g->len;
                    continue;
                }
                ss = ss + ng->len;
                ng = ng->file.next_g;
                gl.match_end = ss - line_start;
                goto NG;
            }

            if (g->use_regex) {
                char end = *(char *)line_end;
                *(char *)line_end = 0;
                int rc = regexec(&g->rx, g->rx_info.fixed_start ? s : line_start, 1, pmatch, 0);
                *(char *)line_end = end;
                if (rc != 0) {
                    s = line_end + 1;
                    continue;
                }
                if (g->rx_info.fixed_start) {
                    gl.match_end = gl.match_start + pmatch[0].rm_eo;
                    gl.match_start += pmatch[0].rm_so;
                } else {
                    gl.match_start = pmatch[0].rm_so;
                    gl.match_end = pmatch[0].rm_eo;
                }
            }

            if (!g->file.callback(&gl))
                goto CLEANUP;
            if (lb->is_binary && g->binary_mode == BINARY)
                goto CLEANUP;

            for (int i = 0; i < g->file.after_lines; i++) {
                const char *ss = line_end + 1;
                line_end = indexbyte(ss, end, '\n');
                if (!line_end) {
                    // Continue printing afterlines when the next line buffer is filled.
                    line_end = end;
                    remain_after_lines = g->file.after_lines - i;
                    break;
                }
                gl.nr++;
                gl.line = ss;
                gl.match_start = gl.match_end = gl.len = line_end - ss;
                gl.is_afterline = true;
                if (!g->file.callback(&gl))
                    goto CLEANUP;
                last_hit = line_end;
            }

            s = line_end + 1;
        }
        buffer_fill(lb, path);
    }
 
CLEANUP:
    close(fd);
    return 0;
}
