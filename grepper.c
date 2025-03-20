#include "grepper.h"
#include "STC/include/stc/csview.h"

#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

static bool maybe_valid_utf8(const char *start, const char *end) 
{
    static uint8_t tab[256] = 
        "1111111111111111111111111111111111111111111111111111111111111111" 
        "1111111111111111111111111111111111111111111111111111111111111111" 
        "0000000000000000000000000000000000000000000000000000000000000000" 
        "2222222222222222222222222222222233333333333333334444444400000000" 
    ;
    while (start < end) {
        int v = tab[(uint8_t)*start] - '0';
        if (!v)
            return false; 
        start += v;
    }
    return start == end;
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

const char *index4bytes(const char *s, const char *end, uint32_t v4, uint32_t mask4)
{
    v4 &= mask4;
    __m256i needle = _mm256_set1_epi32(v4);
    __m256i teeth  = _mm256_set1_epi8((uint8_t)mask4);
    __m256i shuffle1 = _mm256_set_epi8(
            10, 9, 8, 7, 9, 8, 7, 6, 8, 7, 6, 5, 7, 6, 5, 4,
            6, 5, 4, 3, 5, 4, 3, 2, 4, 3, 2, 1, 3, 2, 1, 0
            );
    __m256i shuffle2 = _mm256_set_epi8(
            2, 1, 0, 15, 1, 0, 15, 14, 0, 15, 14, 13, 15, 14, 13, 12,
            14, 13, 12, 11, 13, 12, 11, 10, 12, 11, 10, 9, 11, 10, 9, 8
            );
    __m256i shuffle32 = _mm256_set_epi32(3, 2, 1, 4, 3, 2, 1, 0);

    while (s <= end - 4) {
        __m256i src = _mm256_loadu_si256((__m256i *)s); // may overflow, but we reserved at least 33 bytes after 'end', it's okay.
        src = _mm256_permutevar8x32_epi32(src, shuffle32);
        // for (int i = 0; i < 32; i++) printf("%02x%s", *((uint8_t *)&src + i), i % 4 == 3 ? "|" : " "); printf("\n");

        src = _mm256_and_si256(src, teeth);
        __m256i p1 = _mm256_shuffle_epi8(src, shuffle1);
        __m256i p2 = _mm256_shuffle_epi8(src, shuffle2);
        // for (int i = 0; i < 32; i++) printf("%02x%s", *((uint8_t *)&p1 + i), i % 4 == 3 ? "|" : " "); printf("\n");
        // for (int i = 0; i < 32; i++) printf("%02x%s", *((uint8_t *)&p2 + i), i % 4 == 3 ? "|" : " "); printf("\n");

        __m256i mask1 = _mm256_cmpeq_epi32(p1, needle);
        __m256i mask2 = _mm256_cmpeq_epi32(p2, needle);
        uint32_t matches1 = _mm256_movemask_epi8(mask1);
        uint32_t matches2 = _mm256_movemask_epi8(mask2);
        uint64_t matches = (uint64_t)matches2 << 32 | matches1;
        // printf("%08x\n", matches);
        if (matches) {
            int one = __builtin_ctzll(matches) / 4;
            s += one;
            return s > end - 4 ? NULL : s;
        }
        s += 16;
    }

    return 0;
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

const char *index4bytes(const char *s, const char *end, uint32_t v4, uint32_t mask4)
{
    uint16_t b = v4 >> 16, a = v4;
    uint16x8_t needle = vdupq_n_u16((a & 0x0F0F) | ((b & 0x0F0F) << 4));
    uint8x16_t teeth = vdupq_n_u8(0x0F);
    uint8x16_t shuffle1 = {0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8};
    uint8x16_t shuffle2 = {2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10};
    // printf("%b\n", v4);

    while (s <= end - 16) {
        uint8x16_t p = vandq_u8(vld1q_u8((const uint8_t *)s), teeth);

        uint8x16_t haystack1 = vqtbl1q_u8(p, shuffle1);
        uint8x16_t haystack2 = vqtbl1q_u8(p, shuffle2);
        // for (int i = 0; i < 8; i++) printf("%04x ", *((uint16_t *)&haystack1 + i)); printf("\n");
        // for (int i = 0; i < 4; i++) printf("%04x ", *((uint16_t *)&res1 + i)); printf("\n");

        uint16x8_t res = vreinterpretq_u16_u8(vsliq_n_u8(haystack1, haystack2, 4));
        uint16x8_t mask = vceqq_u16(res, needle);
        uint8x8_t r = vshrn_n_u16(mask, 1);
        uint64_t matches = vget_lane_u64(vreinterpret_u64_u8(r), 0);
        if (matches > 0) {
            int one = __builtin_ctzll(matches) / 8;
            return s + one;
        }
        s += 8;
    }

    while (s < end - 3) {
        if ((*(uint32_t *)s & mask4) == v4)
            return s;
        s++;
    }
    return 0;
}

const char *index2bytes(const char *s, const char *end, uint16_t v, uint8_t mask, uint16_t mask2)
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
    g->find = (char *)malloc(g->len * 3 + 3);
    g->findupper = g->find + g->len + 1;
    g->findlower = g->findupper + g->len + 1;
    memset(g->find, 0, g->len * 3 + 3);
    memset(g->_table, -1, 256 * sizeof(int));
    for (int i = 0; i < g->len; i++){
        if (ignore_case) {
            g->findupper[i] = toupper(find[i]);
            g->findlower[i] = tolower(find[i]);
        } else {
            g->findupper[i] = g->findlower[i] = find[i];
        }
        g->find[i] = find[i];
        g->_table[(uint8_t)g->findlower[i]] = g->_table[(uint8_t)g->findupper[i]] = i;
    }
    g->falses = 0;
    g->next_g = 0;
    g->_case_mask8 = ignore_case ? 0xDF : 0xFF;
    g->_case_mask16 = ignore_case ? 0xDFDF : 0xFFFF;
    g->_case_mask32 = ignore_case ? 0xDFDFDFDF : 0xFFFFFFFF;
}

struct grepper *grepper_add(struct grepper *g, const char *find)
{
    struct grepper *ng = (struct grepper *)malloc(sizeof(struct grepper));
    memset(ng, 0, sizeof(struct grepper));
    grepper_init(ng, find, g->ignore_case);

    while (g->next_g)
        g = g->next_g;
    g->next_g = ng;

    return ng;
}

void grepper_free(struct grepper *g)
{
    if (g->find)
        free(g->find);
    if (g->next_g)
        grepper_free(g->next_g);
    if (g->rx.use_regex)
        cregex_drop(&g->rx.engine);
    if (g->rx.error)
        free(g->rx.error);
}

static const char *indexcasestr_long4(struct grepper *g, const char *s, const char *end)
{
    int64_t lf = g->len;
    uint32_t v = *(uint32_t *)(g->find + lf - 4);
    while (s <= end - lf) {
        s = index4bytes(s + lf - 4, end, v, g->_case_mask32);
        if (!s)
            return s;
        s -= lf - 4;
        int j = lf - 1;
        while (j >= 0 && (s[j] == g->findlower[j] || s[j] == g->findupper[j]))
            --j;
        if (j < 0)
            return s;
		s += MAX(1, j - g->_table[(uint8_t)s[j]]);
    }
	return 0;
}

static const char *indexcasestr_long(struct grepper *g, const char *s, const char *end)
{
    int64_t lf = g->len;
    uint16_t v = *(uint16_t *)(g->find + lf - 2) & g->_case_mask16;
    while (s <= end - lf) {
        const char *s0 = s;
        s = index2bytes(s + lf - 2, end, v, g->_case_mask8, g->_case_mask16);
        if (!s)
            return s;
        s -= lf - 2;
        int j = lf - 1;
        while (j >= 0 && (s[j] == g->findlower[j] || s[j] == g->findupper[j]))
            --j;
        if (j < 0)
            return s;
		s += MAX(1, j - g->_table[(uint8_t)s[j]]);
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

    if (1&&g->len >= 4)
        return indexcasestr_long4(g, s, end);

    return indexcasestr_long(g, s, end);
}

static const char *_indexlf(const char *s, const char *end)
{
    const char *ss = indexbyte(s, end, '\n');
    return ss ? ss : end;
}

static struct grepline *_ctxline(struct grepline *gl, int64_t nr,
        const char *start, const char *end)
{
    gl->nr = nr;
    gl->line = start;
    gl->match_start = gl->match_end = gl->len = end - start;
    gl->is_ctxline = true;
    return gl;
}

int64_t grepper_find(struct grepper *g, const char *s, int64_t ls)
{
    const char *found = indexcasestr(g, s, s + ls);
    return found ? found - s : -1;
}

int grepper_file(struct grepper *g, struct grepper_ctx *ctx)
{
    // const char *zzz = "0123456789abcdefghijklmnop0123456789abcdef";
    // const char *zzzres = index4bytes(zzz, zzz + strlen(zzz), *(uint32_t *)"ijkl", 0xDFDFDFDF);
    // printf("%s\n", zzzres);
    // exit(1);
    const char *beforelines[g->before_lines][2];
    csview rx_match[g->rx.groups];

    int fd = open(ctx->file_name, O_RDONLY);
    if (fd < 0)
        return -1;

    struct linebuf *lb = &ctx->lbuf;
    buffer_reset(lb, fd);
    buffer_fill(lb, ctx->file_name);

    lb->is_binary = indexbyte(lb->buffer, lb->buffer + MIN(lb->len, 1024), 0);
    if (lb->is_binary) {
        switch (g->binary_mode) {
        case BINARY_IGNORE:
            goto CLEANUP;
        case BINARY:
            lb->binary_matching = true;
        }
    }

    int remain_after_lines = 0;

    while (lb->len) {
        struct grepline gl = {
            .ctx = ctx,
            .g = g,
            .nr = lb->lines,
            .is_ctxline = false,
        };
        const char *s = lb->buffer, *last_hit = lb->buffer, *end = lb->buffer + lb->len;

        // Process the remaining afterlines from the prev line buffer.
        for (int i = 0; remain_after_lines > 0 && s < end; i++,remain_after_lines--) {
            const char *line_end = _indexlf(s, end);

            if (!g->callback(_ctxline(&gl, gl.nr, s, line_end)))
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

            const char *line_end = _indexlf(s, end);

            gl.nr += countbyte(last_hit, s, '\n');
            last_hit = s;

            gl.line = line_start;
            gl.len = line_end - line_start;
            gl.match_start = s - line_start;
            gl.match_end = s + g->len - line_start;
            gl.is_ctxline = false;

            struct grepper *ng = g->next_g;
            const char *ss = s + g->len;
NG:
            if (ng) {
                ss = indexcasestr(ng, ss, line_end);
                if (!ss) {
                    s += g->len;
                    continue;
                }
                ss = ss + ng->len;
                ng = ng->next_g;
                gl.match_end = ss - line_start;
                goto NG;
            }

            if (g->rx.use_regex) {
                const char *rx_start = g->rx.fixed_start ? s : line_start;
                // if (!maybe_valid_utf8(rx_start, line_end)) {
                //     lb->is_binary = true;
                //     continue;
                // }
                char end = *line_end;
                // *(char *)line_end = 0;
                int rc = cregex_match_sv(&g->rx.engine, csview_from_n(rx_start, line_end - rx_start), rx_match);
                // *(char *)line_end = end;
                if (rc != CREG_OK) {
                    s = line_end + 1;
                    continue;
                }
                gl.match_start = rx_match[0].buf - line_start;
                gl.match_end = gl.match_start + rx_match[0].size;
            }

            if (g->before_lines) {
                // Print before lines. TODO: print across line buffer
                struct grepline bak = gl;
                int ln = 0;
                for (const char *bs = line_start; bs > lb->buffer && !lb->binary_matching && ln < g->before_lines; ln++) {
                    const char *ss = indexlastbyte(lb->buffer, bs - 1, '\n');
                    ss = ss ? ss + 1 : lb->buffer;
                    beforelines[ln][0] = ss;
                    beforelines[ln][1] = bs - 1;
                    bs = ss;
                }
                for (ln--; ln >= 0; ln--) {
                    if (!g->callback(_ctxline(&gl, bak.nr - ln, beforelines[ln][0], beforelines[ln][1])))
                        goto CLEANUP;
                }
                gl = bak;
            }

            if (!g->callback(&gl))
                goto CLEANUP;

            if (lb->binary_matching)
                goto CLEANUP;

            for (int i = 0; i < g->after_lines; i++) {
                const char *ss = line_end + 1;
                line_end = indexbyte(ss, end, '\n');
                if (!line_end) {
                    // Continue printing afterlines when the next line buffer is filled.
                    line_end = end;
                    remain_after_lines = g->after_lines - i;
                    break;
                }

                if (!g->callback(_ctxline(&gl, gl.nr + 1, ss, line_end)))
                    goto CLEANUP;

                last_hit = line_end;
            }

            s = line_end + 1;
        }
        if (lb->read >= ctx->file_size)
            break;
        buffer_fill(lb, ctx->file_name);
    }
 
CLEANUP:
    close(fd);
    return 0;
}
