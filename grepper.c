#include "grepper.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

static sljit_sw ascii_casecmp(sljit_sw a, sljit_sw b)
{
    struct grepper *g = (struct grepper *)a;
    const char *c = (const char *)b;
    for (int i = 0; i < g->len; i++) {
        if (c[i] != g->findlower[i] && c[i] != g->findupper[i]) return 0;
    }
    return 1;
}

static sljit_sw utf8_casecmp(sljit_sw b, sljit_sw c)
{
    struct grepper *g = (struct grepper *)b;
    const char *a = (const char *)c;
#define CMP if (memcmp(a+i, g->findlower+i, c) != 0 && memcmp(a+i, g->findupper+i, c) != 0) return 0; break;
    for (size_t i = 0; i < g->len;) {
        int n = utf8_chr_size(g->findlower + i);
        switch (n) { // unroll
            case 4: CMP;
            case 3: CMP;
            case 2: CMP;
            case 1: CMP;
            default: __builtin_unreachable();
        }
        i += n;
    }
    return 1;
#undef CMP
}

static func2_t compile_utf8_casecmp(struct grepper *g)
{
    struct sljit_compiler *C = sljit_create_compiler(NULL);

    // S0 = grepper, S1 = text
    // R0 = lower, R1 = upper
    // R2 = lower_head, R3 = upper_head, R4 = text_head
    sljit_emit_enter(C, 0, SLJIT_ARGS2(W, W, W), 5, 2, 0);
    sljit_emit_op1(C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(struct grepper, findlower));
    sljit_emit_op1(C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(struct grepper, findupper));

    for (int i = 0; i < g->len; ) {
        int c = utf8_chr_size(g->findlower + i);
        switch (c) {
        case 1:
            sljit_emit_op1(C, SLJIT_MOV_U8, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R0), (sljit_sw)i);
            sljit_emit_op1(C, SLJIT_MOV_U8, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_R1), (sljit_sw)i);
            sljit_emit_op1(C, SLJIT_MOV_U8, SLJIT_R4, 0, SLJIT_MEM1(SLJIT_S1), (sljit_sw)i);
            break;
        case 2:
            sljit_emit_op1(C, SLJIT_MOV_U16, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R0), (sljit_sw)i);
            sljit_emit_op1(C, SLJIT_MOV_U16, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_R1), (sljit_sw)i);
            sljit_emit_op1(C, SLJIT_MOV_U16, SLJIT_R4, 0, SLJIT_MEM1(SLJIT_S1), (sljit_sw)i);
            break;
        case 3:
            sljit_emit_op1(C, SLJIT_MOV_U32, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R0), (sljit_sw)i);
            sljit_emit_op1(C, SLJIT_MOV_U32, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_R1), (sljit_sw)i);
            sljit_emit_op1(C, SLJIT_MOV_U32, SLJIT_R4, 0, SLJIT_MEM1(SLJIT_S1), (sljit_sw)i);
            sljit_emit_op2(C, SLJIT_AND, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_IMM, 0x00FFFFFFu);
            sljit_emit_op2(C, SLJIT_AND, SLJIT_R3, 0, SLJIT_R3, 0, SLJIT_IMM, 0x00FFFFFFu);
            sljit_emit_op2(C, SLJIT_AND, SLJIT_R4, 0, SLJIT_R4, 0, SLJIT_IMM, 0x00FFFFFFu);
            break;
        case 4:
            sljit_emit_op1(C, SLJIT_MOV_U32, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R0), (sljit_sw)i);
            sljit_emit_op1(C, SLJIT_MOV_U32, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_R1), (sljit_sw)i);
            sljit_emit_op1(C, SLJIT_MOV_U32, SLJIT_R4, 0, SLJIT_MEM1(SLJIT_S1), (sljit_sw)i);
            break;
        }

        struct sljit_jump *next = sljit_emit_cmp(C, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_R4, 0);
        struct sljit_jump *next2 = sljit_emit_cmp(C, SLJIT_EQUAL, SLJIT_R3, 0, SLJIT_R4, 0);
        sljit_emit_return(C, SLJIT_MOV, SLJIT_IMM, 0);
        sljit_set_label(next, sljit_emit_label(C));
        sljit_set_label(next2, sljit_emit_label(C));

        i += c;
    }
    sljit_emit_return(C, SLJIT_MOV, SLJIT_IMM, 1);

    func2_t cmp = sljit_generate_code(C, 0, NULL);
    sljit_free_compiler(C);
    return cmp;
}

int grepper_fixed(struct grepper *g, const char *find)
{
    size_t caseless = 0;

    g->cmp = 0;
    g->next_g = 0;
    g->len = strlen(find);
    assert(g->len);
    g->find = (char *)malloc(g->len * 3 + 12);
    g->findupper = g->find + g->len + 4;
    g->findlower = g->findupper + g->len + 4;
    memcpy(g->find, find, g->len);

    if (!g->ignore_case) 
        return INIT_OK;

    if (g->disable_unicode) {
        for (size_t i = 0; i < g->len; i++) {
            g->findlower[i] = tolower(g->find[i]);
            g->findupper[i] = toupper(g->find[i]);
            if (g->findupper[i] == g->findlower[i])
                caseless++;
        }
        if (caseless == g->len) {
            g->ignore_case = false;
        } else {
            g->cmp = ascii_casecmp;
        }
    } else {
        utf8_decode_t d = {.state=0};
        for (size_t off = 0; off < g->len; ) {
            int n = utf8_decode_codepoint(&d, g->find + off, NULL);
            uint32_t r = d.codep;
            if (r == 0xFFFD)
                return INIT_INVALID_UTF8;
            uint32_t up = utf8_toupper(r), lo = utf8_tolower(r);
            int n1 = utf8_encode(g->findupper + off, up);
            int n2 = utf8_encode(g->findlower + off, lo);
            off += n;

            if (up == lo && up == r) {
                caseless += n;
                continue;
            }
            if (n1 != n2 || n1 != n) 
                return r;
        }
        if (caseless == g->len) {
            g->ignore_case = false;
        } else {
            g->cmp = compile_utf8_casecmp(g);
        }
        // printf("%.*s\n", g->len, g->find);
        // printf("%.*s\n", g->len, g->findupper);
        // printf("%.*s\n", g->len, g->findlower);
    }
    return INIT_OK;
}

void grepper_create(struct grepper *g, const char *s)
{
    int errornumber;
    PCRE2_SIZE erroroffset;
    char tmp[256];

    g->fixed = 0;
    g->re = 0;
    g->rx_error = 0;
    g->find = 0;
    g->findupper = 0;
    g->findlower = 0;
    g->next_g = 0;
    g->cmp = 0;
    g->re = pcre2_compile((PCRE2_SPTR)s, PCRE2_ZERO_TERMINATED,
            (g->ignore_case ? PCRE2_CASELESS : 0) |
            (g->disable_unicode ? 0 : PCRE2_UTF) |
            PCRE2_MATCH_INVALID_UTF,
            &errornumber, &erroroffset, NULL);
    if (!g->re) {
        pcre2_get_error_message(errornumber, (PCRE2_UCHAR *)tmp, sizeof(tmp));
        g->rx_error = (char *)malloc(256 + strlen(s));
        snprintf(g->rx_error, 256 + strlen(s), "invalid pattern at '%s': %s", s + erroroffset, tmp);
        return;
    }
    int jitrc = pcre2_jit_compile(g->re, PCRE2_JIT_COMPLETE);
    if (jitrc != 0)
        WARN("pcre2: JIT not available (code=%d)\n", jitrc);

    struct strings arr = {0};
    struct grepper *cg = NULL;
    bool ignore_case = g->ignore_case;
    g->fixed = extract_fixed(s, g->re, &arr);
    for (int i = 0; i < arr.len; i++) {
        struct grepper *ng = cg == NULL ? g : (struct grepper *)calloc(1, sizeof(struct grepper));
        ng->ignore_case = ignore_case;
        ng->disable_unicode = g->disable_unicode;
        int rc = grepper_fixed(ng, arr.data[i]);
        if (rc == INIT_INVALID_UTF8) {
            g->rx_error = strdup("invalid UTF8 pattern");
            break;
        }
        if (rc != INIT_OK) { // special case characters
            g->fixed = false;
            continue;
        }
        if (cg == NULL) {
            cg = g;
        } else {
            cg->next_g = ng;
            cg = ng;
        }
    }
    strings_free(&arr);
}

#if defined(__x86_64__)

#include <emmintrin.h>
#include <immintrin.h>
#include <smmintrin.h>

int64_t countbyte(const char *s, const char *end, const uint8_t c)
{
    __m256i needle = _mm256_set1_epi8(c);
    int64_t count = 0;
    while (s < end) {
        __m256i haystack = _mm256_loadu_si256((__m256i *)s);
        __m256i res = _mm256_cmpeq_epi8(haystack, needle);
        count += __builtin_popcountll((uint32_t)_mm256_movemask_epi8(res));
        s += 32;
    }
    for (--s; s >= end; --s) {
        if (*s == c) {
            count--;
        }
    }
    return count;
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
    if (*s == a)
      return s;
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

const char *strstr_case(const char* s, size_t n, struct grepper *g)
{
    size_t k = g->len;
    const __m256i firstlo = _mm256_set1_epi8(g->findlower[0]), lastlo = _mm256_set1_epi8(g->findlower[k - 1]);
    const __m256i firstup = _mm256_set1_epi8(g->findupper[0]), lastup = _mm256_set1_epi8(g->findupper[k - 1]);

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
            if (g->cmp((sljit_sw)g, (sljit_sw)s + i + bitpos)) {
                return s + i + bitpos;
            }
        }
    }

    return 0;
}

#elif defined(__aarch64__)

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

    if (s < end) {
        uint8x16_t haystack = vld1q_u8((const uint8_t *)s);
        uint64_t matches = uint8x16_movemask_4bit(vceqq_u8(haystack, needle));
        matches <<= (16 - (end - s)) * 4;
        count += __builtin_popcountll(matches);
    }
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

const char *strstr_case(const char* s, size_t n, struct grepper *g)
{
    size_t k = g->len;
    uint8x16_t first_lo = vdupq_n_u8(g->findlower[0]), first_up = vdupq_n_u8(g->findupper[0]);
    uint8x16_t last_lo = vdupq_n_u8(g->findlower[k - 1]), last_up = vdupq_n_u8(g->findupper[k - 1]);

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
            if (g->cmp((sljit_sw)g, (sljit_sw)s + i + bitpos)) {
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

void grepper_free(struct grepper *g)
{
    if (g->cmp && g->cmp != ascii_casecmp)
        sljit_free_code(g->cmp, NULL);
    if (g->find)
        free(g->find);
    if (g->re)
        pcre2_code_free(g->re);
    if (g->rx_error)
        free(g->rx_error);
    if (g->next_g)
        grepper_free(g->next_g);
}

static const char *indexcasestr(struct grepper *g, const char *s, const char *end)
{
    if (s + g->len > end)
        return 0;

    if (s + g->len == end) {
        if (g->ignore_case)
            return g->cmp((sljit_sw)g, (sljit_sw)s) ? s : 0;
        return memcmp(s, g->find, g->len) == 0 ? s : 0;
    }

    return g->ignore_case ? strstr_case(s, end - s, g) : strstr_x(s, end - s, g->find, g->len);
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

static struct grepline *_init_ctx_grepline(struct grepline *out, struct grepline in,
        int64_t nr, const char *line, int64_t len)
{
    *out = in;
    out->nr = nr;
    out->len = len;
    out->line = line;
    out->match_start = out->match_end = 0;
    out->is_ctxline = true;
    return out;
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

static int _grepfile_iter_prev_chunk(struct grepfile *file, struct grepfile_chunk *part)
{
    if (part->off == 0)
        return FILL_EOF;

    size_t buf_size = part->data_size = MIN(DEFAULT_BUFFER_CAP, part->off);

    // Fullfill buffer to part->data_size.
    for (char *buf = part->buf; buf < part->buf + part->data_size; ) {
        int n = pread(file->fd, buf, buf_size, part->off);
        if (n < 0)
            return errno;
        buf += n;
        buf_size -= n;
    }
    part->off -= part->data_size;
    
    int res;
    if (part->off) {
        const char *start = indexbyte(part->buf, part->buf + part->data_size, '\n');
        if (!start) {
            // FIXME: Long line exceeds the buffer size, expand buffer to read more.
            start = part->buf;
        }
        // Truncate the buffer to full lines.
        part->data_size -= start - part->buf;
        memcpy(part->buf, start, part->data_size);
        res = FILL_OK;
    } else {
        res = FILL_LAST_CHUNK;
    }
    return res;
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
    struct grepline before_lines[g->before_lines];
    struct grepfile *file = part->file;
    struct grepline gl = {
        .g = g,
        .file = part,
        .nr = part->prev_lines,
        .is_ctxline = false,
    };
    const char *s = part->buf, *last_hit = part->buf, *end = part->buf + part->data_size;

    while (s < end) {
        if (g->len == 0) {
            int rc = pcre2_jit_match(g->re, (PCRE2_SPTR)s, end - s, 0, 0, part->match_data, NULL);
            s = rc > 0 ? s + pcre2_get_ovector_pointer(part->match_data)[0] : 0;
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

        if (g->re && !g->fixed) {
            int rc = pcre2_jit_match(g->re,
                    (PCRE2_SPTR)line_start, line_end - line_start, 0, 0,
                    part->match_data, NULL);
            if (rc <= 0) {
                s = line_end + 1;
                continue;
            }
            PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(part->match_data);
            gl.match_start = ovector[0];
            gl.match_end = ovector[1];
        }

        if (g->before_lines) {
            struct grepfile_chunk pp = *part;
            const char *ss = line_start - 1;
            int i = 0;
            for (const char *bound = last_hit, *prev_ss = ss; i < g->before_lines; i++) {
PREV_PREV:
                ss = indexlastbyte(pp.buf, ss, '\n');
                if (!ss) {
                    int res = _grepfile_iter_prev_chunk(file, &pp);
                    if (res == FILL_EOF || res >= 0)
                        break;
                    bound = pp.buf;
                    prev_ss = ss = pp.buf + pp.data_size;
                    goto PREV_PREV;
                }
                if (ss < bound)
                    break;

                int64_t len = prev_ss - (ss + 1);
                _init_ctx_grepline(&before_lines[i], gl, gl.nr - (i + 1), strndup(ss + 1, len), len);
                prev_ss = ss;
            }
            for (; i > 0; i--) {
                g->callback(&before_lines[i - 1]);
                free((void *)before_lines[i - 1].line);
            }
        }

        bool exit_flag = false;

        if (!g->callback(&gl)) {
            FILE_LOCK();
            file->off = file->size;
            FILE_UNLOCK();
            exit_flag = true;
        }

        for (int i = 0; i < g->after_lines; i++) {
            const char *ss = line_end + 1;
            if (ss >= end) {
                exit_flag = true;
                int res = _grepfile_iter_next_chunk(file, part);
                if (res == FILL_EOF || res >= 0)
                    goto EXIT;
                ss = part->buf;
                end = part->buf + part->data_size;
            }

            line_end = indexbyte(ss, end, '\n');
            assert(line_end);
            g->callback(_init_ctx_grepline(&gl, gl, gl.nr + 1, ss, line_end - ss));
        }

        if (exit_flag)
            goto EXIT;

        s = line_end + 1;
    }

EXIT:
    (void)0;
}
