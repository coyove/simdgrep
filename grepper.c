#include "grepper.h"

#include "STC/include/stc/csview.h"

#define i_import
#include "STC/include/stc/cregex.h"
#include "STC/include/stc/utf8.h"

#define RX_SOL 0x80000000
#define RX_EOL 0x40000000

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#ifdef __cplusplus

#include <iostream>
#include <string>

#if defined(__x86_64__)
std::ostream &operator<<(std::ostream &out, const __m128i &v) {
  const uint8_t *p = (const uint8_t *)&v;
  out << '[';
  for (int i = 0; i < 16; i++) {
    out << (int)(*(p + i)) << (i == 15 ? ']' : ' ');
  }
  return out;
}
std::ostream &operator<<(std::ostream &out, const __m256i &v) {
  const uint8_t *p = (const uint8_t *)&v;
  out << '[';
  for (int i = 0; i < 32; i++) {
    out << (int)(*(p + i)) << (i == 31 ? ']' : ' ');
  }
  return out;
}
#endif

#if defined(__aarch64__)
std::ostream &operator<<(std::ostream &out, const uint8x16_t &v) {
  const uint8_t *p = (const uint8_t *)&v;
  out << '[';
  for (int i = 0; i < 16; i++) {
    out << (int)(*(p + i)) << (i == 15 ? ']' : ' ');
  }
  return out;
}
std::ostream &operator<<(std::ostream &out, const uint16x8_t &v) {
  const uint16_t *p = (const uint16_t *)&v;
  out << '[';
  for (int i = 0; i < 8; i++) {
    out << (int)(*(p + i)) << (i == 7 ? ']' : ' ');
  }
  return out;
}
std::ostream &operator<<(std::ostream &out, const uint8x8_t &v) {
  const uint8_t *p = (const uint8_t *)&v;
  out << '[';
  for (int i = 0; i < 8; i++) {
    out << (int)(*(p + i)) << (i == 7 ? ']' : ' ');
  }
  return out;
}
#endif
#endif

static int utf8casecmp(const char *a, const char *b, size_t n)
{
    csview av = csview_from_n(a, n);
    csview bv = csview_from_n(b, n);
    return csview_icmp(&av, &bv);
}

int torune(uint32_t *rune, const char *s)
{
    return chartorune(rune, s);
}

static bool safecase(uint32_t r)
{
    char tmp[4];
    int lo = utf8_tolower(r), hi = utf8_toupper(r);
    return utf8_encode(tmp, lo) == utf8_encode(tmp, hi);
}

static int _rx_skip_bracket(const char *s, int i)
{
    int depth = 1;
    for (++i; depth; ++i) {
        char c = *(s + i);
        if (!c)
            return i;
        if (c != '[' && c != ']')
            continue;
        if (*(s + i - 1) == '\\')
            continue;
        if (c == '[')
            depth++;
        if (c == ']')
            depth--;
    }
    return i;
}

static int _rx_skip_paren(const char *s, int i)
{
    int depth = 1;
    for (++i; depth; ++i) {
        char c = *(s + i);
        if (!c)
            return i;
        if (c != '(' && c != ')' && c != '[')
            continue;
        if (*(s + i - 1) == '\\')
            continue;
        if (c == '[')
            i = _rx_skip_bracket(s, i) - 1; // ++i
        if (c == '(')
            depth++;
        if (c == ')')
            depth--;
    }
    return i;
}

void grepper_init_rx(struct grepper *g, const char *s, bool ignore_case)
{
    int i = 0, j = 0, sl = strlen(s);
    char *pp = (char *)malloc(sl + 1);
    memset(pp, 0, sl + 1);

    for (bool lit = false; i < sl; i++) {
        if (lit) {
            if (s[i] != '\\' || i + 1 >= sl || s[i + 1] != 'E') {
                pp[j++] = s[i];
                continue;
            }
        }
        switch (s[i]) {
        case '|':
            g->rx.use_regex++;
            j = 0;
            goto INIT;
        case '\\':
            if (++i >= sl)
                goto UNSUPPORTED;
            switch (s[i]) {
            case 'Q': lit = true; break;
            case 'E': lit = false; break;
            case 'x':
                if (i + 3 >= sl || s[i + 1] != '{')
                    goto UNSUPPORTED;
                char *rend;
                i += 2;
                int r = strtol(s + i, &rend, 16);
                if (*rend != '}' || r < 0 || r > 0x10FFFF)
                    goto UNSUPPORTED;
                i = rend - s;
                if (ignore_case && !safecase(r)) {
                    g->rx.use_regex++;
                    pp[j++] = 0;
                } else {
                    j += utf8_encode(pp + j, r);
                }
                break;
            case 'r': pp[j++] = '\r'; break;
            case 't': pp[j++] = '\t'; break;
            case 'v': pp[j++] = '\v'; break;
            case 'f': pp[j++] = '\f'; break;
            case 'a': pp[j++] = '\a'; break;
            case 's': case 'S': case 'w': case 'W': case 'd': case 'D':
            case 'b': case 'B': case 'A': case 'z': case 'Z': case 'p': 
            case 'P':
                g->rx.use_regex++;
                pp[j++] = 0;
                break;
            default:
                pp[j++] = s[i];
            }
            break;
        case '.':
            pp[j++] = 0;
            g->rx.use_regex++;
            break;
        case '^':
            g->rx.use_regex |= RX_SOL;
            break;
        case '$':
            g->rx.use_regex |= RX_EOL;
            break;
        case '+': 
            g->rx.use_regex++;
            break;
        case '(':
            g->rx.use_regex++;
            pp[j++] = 0;
            i = _rx_skip_paren(s, i) - 1; // ++i
            break;
        case '{':
            g->rx.use_regex++;
            for (++i; s[i] && s[i] != '}'; ++i);
            // fallthrough
        case '*': case '?': 
            g->rx.use_regex++;
            for (j--; j && ((uint8_t)pp[j] >> 6) == 2; j--);
            if (j < 0)
                goto UNSUPPORTED;
            if (j)
                pp[j++] = 0;
            break;
        case '[':
            g->rx.use_regex++;
            pp[j++] = 0;
            i = _rx_skip_bracket(s, i) - 1; // ++i
            break;
        default:
            if (ignore_case) {
                uint32_t r;
                int n = torune(&r, s + i);
                memcpy(pp + j, s + i, n);
                i += n - 1;
                if (!safecase(r)) {
                    g->rx.use_regex++;
                    pp[j++] = 0;
                    break;
                }
                j += n;
            } else {
                pp[j++] = s[i];
            }
            if (!g->rx.use_regex)
                g->rx.fixed_start = true;
        }
    }

INIT:
    if (g->rx.use_regex == RX_SOL) {
        // ^abc
        g->rx.use_regex = 0;
        g->rx.line_start = true;
    } else if (g->rx.use_regex == RX_EOL) {
        // abc$
        g->rx.use_regex = 0;
        g->rx.line_end = true;
    } else if (g->rx.use_regex == RX_SOL + RX_EOL) {
        // ^abc$
        g->rx.use_regex = 0;
        g->rx.line_start = g->rx.line_end = true;
    } else if (g->rx.use_regex) {
        g->rx.engine = cregex_make(s, ignore_case ? CREG_ICASE : 0);
        if (g->rx.engine.error != CREG_OK) {
            g->rx.use_regex = 0;
            g->rx.error = (char *)malloc(256);
            snprintf(g->rx.error, 256, "regexp error (%d)", g->rx.engine.error);
        } else {
            g->rx.groups = cregex_captures(&g->rx.engine) + 1;
        }
    }

    for (int i = 0 ; i < j; ) {
        int ln = strlen(pp + i);
        if (ln) {
            if (!g->find) {
                grepper_init(g, pp + i, ignore_case);
            } else {
                grepper_add(g, pp + i);
            }
        }
        i += ln + 1;
    }
    if (!g->find) {
        grepper_init(g, "<SLOWRX>", false);
        g->slow_rx = true;
    }

    free(pp);
    return;

UNSUPPORTED:
    free(pp);
    g->rx.error = (char *)malloc(256);
    snprintf(g->rx.error, 256, "invalid escape at '%s'", s + i - 1);
}

bool grepper_match(struct grepper *g, struct grepline *gl, csview *rx_match,
        const char *line_start, const char *s, const char *line_end)
{
    if (g->rx.use_regex) {
        const char *rx_start = g->rx.fixed_start ? s : line_start;
        char end_ch = *line_end;

        *(char *)line_end = 0;
        int rc = cregex_match_sv(&g->rx.engine, csview_from_n(rx_start, line_end - rx_start), rx_match, CREG_DEFAULT);
        *(char *)line_end = end_ch;

        if (rc != CREG_OK)
            return false;

        gl->match_start = rx_match[0].buf - line_start;
        gl->match_end = gl->match_start + rx_match[0].size;
        return true;
    }
    if (g->rx.line_start && s != line_start)
        return false;
    if (g->rx.line_end && s + g->len != line_end)
        return false;
    return true;
}

#if defined(__x86_64__)

#include <emmintrin.h>
#include <immintrin.h>
#include <smmintrin.h>

int64_t countbyte(const char *s, const char *end, const uint8_t c) {
    __m256i needle = _mm256_set1_epi8(c);
    int64_t count = 0;
    while (s < end) {
        __m256i haystack = _mm256_loadu_si256((__m256i *)s);
        __m256i res = _mm256_cmpeq_epi8(haystack, needle);
        count += __builtin_popcountll(_mm256_movemask_epi8(res));
        s += 32;
    }
    for (--s; s >= end; --s) {
        if (*s == c) {
            count--;
        }
    }
    return count;
}

const char *indexlastbyte(const char *start, const char *s, const uint8_t a) {
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
    if (*s == a)
      return s;
  }
  return 0;
}

const char *indexbyte(const char *s, const char *end, const uint8_t a) {
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
    if (*s == a)
      return s;
  }
  return 0;
}

const char *strstr_x(const char* s, size_t n, const char* needle, size_t k)
{
    const __m256i first = _mm256_set1_epi8(needle[0]);
    const __m256i last  = _mm256_set1_epi8(needle[k - 1]);
    n -= k - 1;
    for (size_t i = 0; i < n; i += 64) {
        const __m256i block_first1 = _mm256_loadu_si256((const __m256i*)(s + i));
        const __m256i block_last1  = _mm256_loadu_si256((const __m256i*)(s + i + k - 1));

        const __m256i block_first2 = _mm256_loadu_si256((const __m256i*)(s + i + 32));
        const __m256i block_last2  = _mm256_loadu_si256((const __m256i*)(s + i + k - 1 + 32));

        const __m256i eq_first1 = _mm256_cmpeq_epi8(first, block_first1);
        const __m256i eq_last1  = _mm256_cmpeq_epi8(last, block_last1);

        const __m256i eq_first2 = _mm256_cmpeq_epi8(first, block_first2);
        const __m256i eq_last2  = _mm256_cmpeq_epi8(last, block_last2);

        const uint32_t mask1 = _mm256_movemask_epi8(_mm256_and_si256(eq_first1, eq_last1));
        const uint32_t mask2 = _mm256_movemask_epi8(_mm256_and_si256(eq_first2, eq_last2));
        uint64_t mask = mask1 | ((uint64_t)mask2 << 32);

        while (mask != 0) {
            const int bitpos = __builtin_ctzll(mask);
            if (i + bitpos >= n)
                return 0;
            if (memcmp(s + i + bitpos + 1, needle + 1, k - 2) == 0) {
                return s + i + bitpos;
            }
            mask = bitpos == 63 ? 0 : (mask >> (bitpos + 1) << (bitpos + 1));
        }
    }

    return 0;
}

const char *strstr_case(const char* s, size_t n, const char* lo, const char *up, size_t k)
{
    const __m256i firstlo = _mm256_set1_epi8(lo[0]), lastlo = _mm256_set1_epi8(lo[k - 1]);
    const __m256i firstup = _mm256_set1_epi8(up[0]), lastup = _mm256_set1_epi8(up[k - 1]);

    n -= k - 1;
    for (size_t i = 0; i < n; i += 32) {
        const __m256i block_first1 = _mm256_loadu_si256((const __m256i*)(s + i));
        const __m256i block_last1  = _mm256_loadu_si256((const __m256i*)(s + i + k - 1));

        const __m256i eq_first1 = _mm256_or_si256(_mm256_cmpeq_epi8(firstlo, block_first1), _mm256_cmpeq_epi8(firstup, block_first1));
        const __m256i eq_last1 = _mm256_or_si256(_mm256_cmpeq_epi8(lastlo, block_last1), _mm256_cmpeq_epi8(lastup, block_last1));

        uint64_t mask = (uint32_t)_mm256_movemask_epi8(_mm256_and_si256(eq_first1, eq_last1));

        while (mask != 0) {
            const int bitpos = __builtin_ctzll(mask);
            mask &= ~(1LLU << bitpos);
            if (i + bitpos >= n)
                return 0;
            if (utf8casecmp(s + i + bitpos + 1, lo + 1, k - 2) == 0) {
                return s + i + bitpos;
            }
        }
    }

    return 0;
}

#elif defined(__aarch64__)

#include <arm_neon.h>

#define uint8x16_movemask_4bit(x) vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(x), 4)), 0);

int64_t countbyte(const char *s, const char *end, uint8_t c) {
    int64_t count = 0;

    uint8x16_t needle = vdupq_n_u8(c);
    while (s <= end - 16) {
        uint8x16_t haystack = vld1q_u8((const uint8_t *)s);
        uint64_t matches = uint8x16_movemask_4bit(vceqq_u8(haystack, needle));
        count += __builtin_popcountll(matches);
        s += 16;
    }

    uint8x16_t haystack = vld1q_u8((const uint8_t *)s);
    uint64_t matches = uint8x16_movemask_4bit(vceqq_u8(haystack, needle));
    matches <<= (16 - (end - s)) * 4;
    count += __builtin_popcountll(matches);
    return count / 4;
}

const char *strstr_x(const char* s, size_t n, const char* needle, size_t k)
{
    uint8x16_t first8 = vdupq_n_u8(needle[0]), last8 = vdupq_n_u8(needle[k - 1]);
    n -= k - 1;
    for (size_t i = 0; i < n; i += 16) {
        uint8x16_t first = vld1q_u8((const uint8_t *)s + i);
        uint8x16_t last = vld1q_u8((const uint8_t *)s + i + k - 1);

        first = vceqq_u8(first, first8);
        last = vceqq_u8(last, last8);

        uint64_t mask = uint8x16_movemask_4bit(vandq_s8(first, last));
        while (mask != 0) {
            int bitpos = __builtin_ctzll(mask);
            mask &= ~(0xFLLU << bitpos);
            bitpos /= 4;
            if (i + bitpos >= n)
                return 0;
            if (memcmp(s + i + bitpos + 1, needle + 1, k - 2) == 0) {
                return s + i + bitpos;
            }
        }
    }

    return 0;
}

const char *strstr_case(const char* s, size_t n, const char *lo, const char *up, size_t k)
{
    uint8x16_t first_lo = vdupq_n_u8(lo[0]), first_up = vdupq_n_u8(up[0]);
    uint8x16_t last_lo = vdupq_n_u8(lo[k - 1]), last_up = vdupq_n_u8(up[k - 1]);

    n -= k - 1;
    for (size_t i = 0; i < n; i += 16) {
        uint8x16_t first = vld1q_u8((const uint8_t *)s + i);
        uint8x16_t last = vld1q_u8((const uint8_t *)s + i + k - 1);

        first = vorrq_u8(vceqq_u8(first, first_lo), vceqq_u8(first, first_up));
        last = vorrq_u8(vceqq_u8(last, last_lo), vceqq_u8(last, last_up));

        uint64_t mask = uint8x16_movemask_4bit(vandq_s8(first, last));
        // asm volatile(
        //         "ld1 { v0.16b }, [%1]\n" // v0 = first block
        //         "ld1 { v1.16b }, [%2]\n" // v1 = last block
        //         "cmeq.16b v2, v0, v6\n"
        //         "cmeq.16b v0, v0, v7\n"
        //         "orr.16b  v0, v0, v2\n" // compare first block
        //         "cmeq.16b v2, v1, v8\n"
        //         "cmeq.16b v1, v1, v9\n"
        //         "orr.16b  v1, v1, v2\n" // compare last block
        //         "and.16b  v0, v0, v1\n"
        //         "shrn.8b  v0, v0, #0x4\n"
        //         "umov %0, v0.d[0]\n"
        //         : "=r"(mask)
        //         : "r"(s + i), "r"(s + i + k - 1)
        //         : "memory");

        while (mask != 0) {
            int bitpos = __builtin_ctzll(mask);
            mask &= ~(0xFLLU << bitpos);
            bitpos /= 4;
            if (i + bitpos >= n)
                return 0;
            if (utf8casecmp(s + i + bitpos + 1, lo + 1, k - 2) == 0) {
                return s + i + bitpos;
            }
        }
    }

    return 0;
}

const char *indexbyte(const char *s, const char *end, const uint8_t a) {
    uint8x16_t needle = vdupq_n_u8(a);

    while (s <= end - 16) {
        uint8x16_t haystack = vld1q_u8((const uint8_t *)s);
        uint64_t m = uint8x16_movemask_4bit(vceqq_u8(haystack, needle));
        if (m > 0)
            return s + __builtin_ctzll(m) / 4;
        s += 16;
    }

    for (; s < end; s++) {
        if (*s == a)
            return s;
    }
    return 0;
}

const char *indexlastbyte(const char *start, const char *s, const uint8_t a) {
    uint8x16_t needle = vdupq_n_u8(a);

    while (start <= s - 16) {
        s -= 16;
        uint8x16_t haystack = vld1q_u8((const uint8_t *)s);
        uint64_t m = uint8x16_movemask_4bit(vceqq_u8(haystack, needle));
        if (m > 0)
            return s + 15 - __builtin_clzll(m) / 4;
    }

    for (s--; s >= start; s--) {
        if (*s == a)
            return s;
    }
    return 0;
}

#endif

int grepper_init(struct grepper *g, const char *find, bool ignore_case) {
    g->ignore_case = ignore_case;
    g->len = strlen(find);

    if (!ignore_case) {
        g->find = strdup(find);
    } else {
        g->find = (char *)malloc(g->len * 3 + 12);
        g->findupper = g->find + g->len + 4;
        g->findlower = g->findupper + g->len + 4;
        memcpy(g->find, find, g->len);
        utf8_decode_t d = {.state=0};
        for (size_t off = 0; off < g->len; ) {
            uint32_t r;
            int n = torune(&r, find + off);
            if (r == 0xFFFD)
                return INIT_INVALID_UTF8;
            uint32_t up = utf8_toupper(r), lo = utf8_tolower(r);
            int n1 = utf8_encode(g->findupper + off, up);
            int n2 = utf8_encode(g->findlower + off, lo);
            if (n1 != n2 || n1 != n) 
                return r;
            off += n;
        }
        // printf("%.*s\n", g->len, g->find);
        // printf("%.*s\n", g->len, g->findupper);
        // printf("%.*s\n", g->len, g->findlower);
    }

    g->next_g = 0;
    return INIT_OK;
}

struct grepper *grepper_add(struct grepper *g, const char *find) {
    struct grepper *ng = (struct grepper *)malloc(sizeof(struct grepper));
    memset(ng, 0, sizeof(struct grepper));
    grepper_init(ng, find, g->ignore_case);

    while (g->next_g)
        g = g->next_g;
    g->next_g = ng;

    return ng;
}

void grepper_free(struct grepper *g) {
  if (g->find)
    free(g->find);
  if (g->next_g)
    grepper_free(g->next_g);
  if (g->rx.use_regex)
    cregex_drop(&g->rx.engine);
  if (g->rx.error)
    free(g->rx.error);
}

static const char *indexcasestr(struct grepper *g, const char *s, const char *end) {
    if (s + g->len > end)
        return 0;

    if (s + g->len == end) {
        if (g->ignore_case)
            return utf8casecmp(s, g->find, g->len) == 0 ? s : 0;
        return memcmp(s, g->find, g->len) == 0 ? s : 0;
    }

    return g->ignore_case ?
        strstr_case(s, end - s, g->findlower, g->findupper, g->len) :
        strstr_x(s, end - s, g->find, g->len);
}

int64_t grepper_find(struct grepper *g, const char *s, int64_t ls)
{
    const char *found = indexcasestr(g, s, s + ls);
    return found ? found - s : -1;
}

#ifdef __APPLE__
#define FILE_LOCK() os_unfair_lock_lock(&file->lock)
#define FILE_UNLOCK() os_unfair_lock_unlock(&file->lock)
#else
#define FILE_LOCK() pthread_mutex_lock(&file->lock)
#define FILE_UNLOCK() pthread_mutex_unlock(&file->lock)
#endif

int grepfile_inc_ref(struct grepfile *file)
{
    int r;
    FILE_LOCK();
    if (file->off >= file->size) {
        r = file->chunk_refs == 0 ? INC_FREEABLE : INC_WAIT_FREEABLE;
    } else {
        file->chunk_refs++;
        r = INC_NEXT;
    }
    FILE_UNLOCK();
    return r;
}

void grepfile_dec_ref(struct grepfile *file)
{
    FILE_LOCK();
    assert(file->chunk_refs > 0);
    file->chunk_refs--;
    FILE_UNLOCK();
}

static void grepfile_chunk_grow(struct grepfile_chunk *part, size_t n)
{
    if (n <= part->cap_size) {
        part->buf_size = n;
    } else {
        part->buf_size = part->cap_size = n;
        char *x = (char *)malloc(n + 64);
        memcpy(x, part->buf, part->data_size);
        free(part->buf);
        part->buf = x;
    }
}

static int _grepfile_iter_next_chunk(struct grepfile *file, struct grepfile_chunk *part)
{
    part->off += part->data_size;
    part->data_size = 0;

    if (part->off >= file->size)
        return FILL_EOF;

    // Fill buffer.
    char *buf = part->buf;
    size_t buf_size = part->buf_size;
    int n, res;

READ:
    n = pread(file->fd, buf, buf_size, part->off);
    if (n < 0)
        return errno;
    
    // Calc total lines.
    if ((file->status & STATUS_IS_BINARY_MATCHING) == 0)
        part->prev_lines += countbyte(buf, buf + n, '\n');

    if (part->off + n < file->size) {
        const char *end = indexlastbyte(buf, buf + n, '\n');
        if (!end) {
            // Long line exceeds the buffer size, expand buffer to read more.
            part->data_size += n;
            part->off += n;
            grepfile_chunk_grow(part, part->buf_size * 3 / 2);
            buf = part->buf + part->data_size;
            buf_size = part->buf_size - part->data_size;
            goto READ;
        }
        // Truncate the buffer to full lines.
        n = end + 1 - buf;
        res = FILL_OK;
    } else {
        res = FILL_LAST_CHUNK;
    }

    part->data_size += n;
    return res;
}

int grepfile_acquire_chunk(struct grepfile *file, struct grepfile_chunk *part)
{
    FILE_LOCK();

    if (file->off >= file->size) {
        FILE_UNLOCK();
        return FILL_EOF;
    }

    // Record how many lines before this part.
    part->off = file->off;
    part->prev_lines = file->lines;
    part->file = file;
    part->data_size = 0;
    part->buf_size = DEFAULT_BUFFER_CAP;

    // Fill buffer.
    char *buf = part->buf;
    size_t buf_size = part->buf_size;
    int n, res;

READ:
    n = pread(file->fd, buf, buf_size, file->off);
    if (n < 0) {
        // Only one caller will see the error (then process), others will get EOF.
        file->off = file->size;
        FILE_UNLOCK();
        return errno;
    }
    
    // Calc total lines.
    if ((file->status & STATUS_IS_BINARY_MATCHING) == 0)
        file->lines += countbyte(buf, buf + n, '\n');

    if (file->off + n < file->size) {
        const char *end = indexlastbyte(buf, buf + n, '\n');
        if (!end) {
            // Long line exceeds the buffer size, expand buffer to read more.
            part->data_size += n;
            file->off += n;
            grepfile_chunk_grow(part, part->buf_size * 3 / 2);
            buf = part->buf + part->data_size;
            buf_size = part->buf_size - part->data_size;
            goto READ;
        }
        // Truncate the buffer to full lines.
        n = end + 1 - buf;
        res = FILL_OK;
    } else {
        res = FILL_LAST_CHUNK;
    }

    part->data_size += n;
    file->off += n;

    FILE_UNLOCK();
    return res;
}

int grepfile_open(struct grepper *g, struct grepfile *file, struct grepfile_chunk *part)
{
    if (!matcher_match(file->root_matcher, file->name, false, part->buf, part->buf_size))
        return OPEN_IGNORED;

    int fd = open(file->name, O_RDONLY);
    if (fd < 0)
        return errno;

    file->lines = 0;
    file->off = 0;
    file->status = STATUS_OPENED;
    file->fd = fd;
    file->size = lseek(fd, 0, SEEK_END);
    file->chunk_refs = 1;
    if (file->size < 0)
        return errno;
    if (file->size == 0)
        return OPEN_EMPTY;

    int res = grepfile_acquire_chunk(file, part);
    if (res == FILL_LAST_CHUNK || res == FILL_OK) {
        // Fill okay.
    } else if (res == FILL_EOF) {
        __builtin_unreachable();
    } else {
        return errno;
    }

    if (indexbyte(part->buf, part->buf + MIN(1024, part->data_size), 0)) {
        file->status |= STATUS_IS_BINARY;
        switch (g->binary_mode) {
            case BINARY_IGNORE:
                return OPEN_BINARY_SKIPPED;
            case BINARY:
                file->status |= STATUS_IS_BINARY_MATCHING;
        }
    }
    return res;
}

int grepfile_release(struct grepfile *file)
{
    if (file->fd)
        close(file->fd);
#ifndef __APPLE__
    pthread_mutex_destroy(&file->lock); 
#endif
    free(file->name);
    free(file);
    return 1;
}

void grepfile_process_chunk(struct grepper *g, struct grepfile_chunk *part)
{
    struct grepfile *file = part->file;
    csview rx_match[g->rx.groups];
    struct grepline gl = {
        .g = g,
        .file = part,
        .nr = part->prev_lines,
        .is_ctxline = false,
    };
    const char *s = part->buf, *last_hit = part->buf, *end = part->buf + part->data_size;

    while (s < end) {
        if (g->slow_rx) {
            int rc = cregex_match_sv(&g->rx.engine, csview_from_n(s, end - s), rx_match, CREG_DEFAULT);
            s = rc != CREG_OK ? 0 : rx_match[0].buf;
        } else {
            s = indexcasestr(g, s, end);
        }
        if (!s)
            goto EXIT;

        const char *line_start = indexlastbyte(part->buf, s, '\n');
        line_start = line_start ? line_start + 1 : part->buf;

        const char *line_end = indexbyte(s, end, '\n');
        if (!line_end)
            line_end = end;

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

        if (!grepper_match(g, &gl, rx_match, line_start, s, line_end)) {
            s = line_end + 1;
            continue;
        }

        if (!g->callback(&gl)) {
            FILE_LOCK();
            file->off = file->size;
            FILE_UNLOCK();
            goto EXIT;
        }

        bool iter = false;
        for (int i = 0; i < g->after_lines; i++) {
            const char *ss = line_end + 1;
            if (ss >= end) {
                iter = true;
                int res = _grepfile_iter_next_chunk(file, part);
                if (res == FILL_EOF || res >= 0)
                    goto EXIT;
                ss = part->buf;
                end = part->buf + part->data_size;
            }

            line_end = indexbyte(ss, end, '\n');
            assert(line_end);
            gl.nr++;
            gl.line = ss;
            gl.match_start = gl.match_end = gl.len = line_end - ss;
            gl.is_ctxline = true;
            g->callback(&gl);
        }
        if (iter)
            goto EXIT;

        s = line_end + 1;
    }

EXIT:
    (void)0;
}
