#include "grepper.h"

#include "STC/include/stc/csview.h"

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

#if defined(__x86_64__)

#include <emmintrin.h>
#include <immintrin.h>
#include <smmintrin.h>

int64_t countbyte(const char *s, const char *end, const uint8_t c) {
    __m256i needle = _mm256_set1_epi8(c);
    int64_t count = 0;
    while (end - s >= 32) {
        __m256i haystack = _mm256_loadu_si256((__m256i *)s);
        __m256i res = _mm256_cmpeq_epi8(haystack, needle);
        count += __builtin_popcountll(_mm256_movemask_epi8(res));
        s += 32;
    }
    // __m256i haystack = _mm256_loadu_si256((__m256i *)s);
    // __m256i res = _mm256_cmpeq_epi8(haystack, needle);
    // count += __builtin_popcountll(_mm256_movemask_epi8(res) << (s + 32 - end));
    while (s < end) {
        if (*s++ == c)
            count++;
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
    assert(k > 0);
    assert(n > 0);
    const __m256i first = _mm256_set1_epi8(needle[0]);
    const __m256i last  = _mm256_set1_epi8(needle[k - 1]);
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

#elif defined(__aarch64__)

#include <arm_neon.h>

int64_t countbyte(const char *s, const char *end, uint8_t c) {
    // uint8x16_t needle = vdupq_n_u8(c);
    asm volatile("dup.16b v9, %w0\n" : : "r"(c) : "memory");
    int64_t count = 0;
    uint32_t temp;

    while (end - s >= 16) {
        asm volatile(
                "ld1 { v0.16b }, [%2]\n"
                "cmeq.16b  v0, v0, v9\n"
                "shrn.8b   v0, v0, #0x4\n"
                "cnt.8b    v0, v0\n"
                "uaddlv.8b h1, v0\n"
                "fmov      %w1, s1\n"
                "add       %0, %3, %x1\n"
                : "=r"(count), "=r"(temp)
                : "r"(s), "r"(count)
                : "memory");
    //     uint8x16_t haystack = vld1q_u8((const uint8_t *)s);
    //     uint8x16_t res = vceqq_u8(haystack, needle);
    //     uint8x8_t r = vshrn_n_u16(res, 4);
    //     uint64_t matches = vget_lane_u64(vreinterpret_u64_u8(r), 0);
    //     count += __builtin_popcountll(matches);
        // count += (int64_t)temp;
        s += 16;
    }
    count /= 4;

    while (s < end) {
        if (*s++ == c)
            count++;
    }
    return count;
}

const char *strstr_x(const char* s, size_t n, const char* needle, size_t k)
{
    asm volatile(
            "dup.16b v7, %w0\n"
            "dup.16b v8, %w1\n"
            :
            : "r"(needle[0]), "r"(needle[k - 1])
            : "memory");

    for (size_t i = 0; i < n; i += 16) {
        uint64_t mask;
        
        asm volatile(
                "ld1 { v0.16b }, [%1]\n" // v0 = first block
                "ld1 { v1.16b }, [%2]\n" // v1 = last block
                "cmeq.16b v0, v0, v7\n"
                "cmeq.16b v1, v1, v8\n"
                "and.16b  v0, v0, v1\n"
                "shrn.8b  v0, v0, #0x4\n"
                "umov %0, v0.d[0]\n"
                : "=r"(mask)
                : "r"(s + i), "r"(s + i + k - 1)
                : "memory");

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

const char *strstr_case(const char* s, const size_t n, const char* needle, const size_t k)
{
    asm volatile(
            "dup.16b v7, %w0\n"
            "dup.16b v8, %w1\n"
            "movi.16b v9, #0xDF\n"
            :
            : "r"(needle[0] & 0xDF), "r"(needle[k - 1] & 0xDF)
            : "memory");

    for (size_t i = 0; i < n; i += 16) {
        uint64_t mask;
        
        asm volatile(
                "ld1 { v0.16b }, [%1]\n" // v0 = first block
                "ld1 { v1.16b }, [%2]\n" // v1 = last block
                "and.16b  v0, v0, v9\n"
                "cmeq.16b v0, v0, v7\n"
                "and.16b  v1, v1, v9\n"
                "cmeq.16b v1, v1, v8\n"
                "and.16b  v0, v0, v1\n"
                "shrn.8b  v0, v0, #0x4\n"
                "umov %0, v0.d[0]\n"
                : "=r"(mask)
                : "r"(s + i), "r"(s + i + k - 1)
                : "memory");

        while (mask != 0) {
            int bitpos = __builtin_ctzll(mask);
            mask &= ~(0xFLLU << bitpos);
            bitpos /= 4;
            if (i + bitpos >= n)
                return 0;
            if (strncasecmp(s + i + bitpos + 1, needle + 1, k - 2) == 0) {
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
    uint16x8_t mask = vreinterpretq_u16_u8(vceqq_u8(haystack, needle));
    uint8x8_t res = vshrn_n_u16(mask, 4);
    uint64_t m = vreinterpret_u64_u8(res);
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

void grepper_init(struct grepper *g, const char *find, bool ignore_case) {
  g->ignore_case = ignore_case;
  g->len = strlen(find);
  g->find = (char *)malloc(g->len * 3 + 3);
  g->findupper = g->find + g->len + 1;
  g->findlower = g->findupper + g->len + 1;
  memset(g->find, 0, g->len * 3 + 3);
  memset(g->_table, -1, 256 * sizeof(int));

  if (ignore_case) {
    for (int i = 0, j = 0; i < g->len;) {
      uint32_t r;
      int n = torune(&r, find + i);
      utf8_encode(g->findlower + j, utf8_tolower(r));
      utf8_encode(g->findupper + j, utf8_toupper(r));
      for (; n; n--, i++, j++) {
        g->find[i] = find[i];
        g->_table[(uint8_t)g->findlower[j]] = i;
        g->_table[(uint8_t)g->findupper[j]] = i;
      }
    }
  } else {
    for (int i = 0; i < g->len; i++) {
      g->find[i] = g->findupper[i] = g->findlower[i] = find[i];
      g->_table[(uint8_t)find[i]] = i;
    }
  }

  g->falses = 0;
  g->next_g = 0;
  g->_case_mask8 = ignore_case ? 0xDF : 0xFF;
  g->_case_mask16 = ignore_case ? 0xDFDF : 0xFFFF;
  g->_case_mask32 = ignore_case ? 0xDFDFDFDF : 0xFFFFFFFF;
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
            return strncasecmp(s, g->find, g->len) == 0 ? s : 0;
        return strncmp(s, g->find, g->len) == 0 ? s : 0;
    }

    return g->ignore_case ?
        strstr_case(s, end - s, g->find, g->len) :
        strstr_x(s, end - s, g->find, g->len);
}

int64_t grepper_find(struct grepper *g, const char *s, int64_t ls)
{
    const char *found = indexcasestr(g, s, s + ls);
    return found ? found - s : -1;
}

bool is_buffer_eof(struct grepfile *file, struct grepfile_chunk *part)
{
    if (file->off >= file->size)
        return true;
    pthread_mutex_lock(&file->lock);
    bool eof = file->off >= file->size;
    pthread_mutex_unlock(&file->lock);
    return eof;
}

int buffer_fill(struct grepfile *file, struct grepfile_chunk *part)
{
    pthread_mutex_lock(&file->lock);

    if (file->off >= file->size) {
        pthread_mutex_unlock(&file->lock);
        return FILL_EOF;
    }

    // Record how many lines before this part.
    part->prev_lines = file->lines;
    part->is_binary = file->is_binary;
    part->is_binary_matching = file->is_binary_matching;
    if (part->name)
        free(part->name);
    part->name = strdup(file->name);

    // Fill buffer.
    int n = pread(file->fd, part->buf, part->buf_size, file->off);
    if (n < 0) {
        // Only one caller will see the error (then process), others will get EOF.
        file->off = file->size;
        pthread_mutex_unlock(&file->lock);
        return errno;
    }
    
    // Calc total lines.
    if (!file->is_binary_matching)
        file->lines += countbyte(part->buf, part->buf + n, '\n');

    int res;
    if (file->off + n < file->size) {
        // Truncate the buffer to full lines.
        const char *end = indexlastbyte(part->buf, part->buf + n, '\n');
        if (end)
            n = end + 1 - part->buf;
        res = FILL_OK;
    } else {
        res = FILL_LAST_CHUNK;
    }
    part->data_size = n;
    file->off += n;
    pthread_mutex_unlock(&file->lock);
    return res;
}

int grepfile_open(const char *file_name, struct grepper *g, struct grepfile *file, struct grepfile_chunk *part)
{
    int fd = open(file_name, O_RDONLY);
    if (fd < 0)
        return errno;

    size_t fs = lseek(fd, 0, SEEK_END);
    if (fs < 0)
        return errno;
    if (fs == 0) {
        close(fd);
        return FILL_EMPTY;
    }

    file->fd = fd;
    file->name = file_name;
    file->size = fs;
    file->lines = 0;
    file->off = 0;
    file->is_binary_matching = 0;
    pthread_mutex_init(&file->lock, NULL);

    int res = buffer_fill(file, part);
    if (res == FILL_LAST_CHUNK) {
    } else if (res == FILL_OK) {
    } else if (res == FILL_EOF) {
    } else {
        return errno;
    }

    part->is_binary = file->is_binary = indexbyte(part->buf, part->buf + MIN(1024, part->data_size), 0);
    if (file->is_binary) {
        switch (g->binary_mode) {
            case BINARY_IGNORE:
                return OPEN_BINARY_SKIPPED;
            case BINARY:
                part->is_binary_matching = file->is_binary_matching = true;
        }
    }
    return res;
}

int grepfile_release(struct grepfile *file)
{
    if (file->fd)
        close(file->fd);
    pthread_mutex_destroy(&file->lock); 
    return 1;
}

bool grepfile_process(struct grepper *g, struct grepfile_chunk *part)
{
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
            char ch = *end;
            *(char *)end = 0;
            int rc = cregex_match_sv(&g->rx.engine, csview_from_n(s, end - s), rx_match, CREG_DEFAULT);
            *(char *)end = ch;
            s = rc != CREG_OK ? 0 : rx_match[0].buf;
        } else {
            s = indexcasestr(g, s, end);
        }
        if (!s)
            break;

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

        if (!g->callback(&gl))
            return false;

        for (int i = 0; i < g->after_lines; i++) {
            const char *ss = line_end + 1;
            line_end = indexbyte(ss, end, '\n');
            if (!line_end) {
                // Continue printing afterlines when the next line buffer is filled.
                line_end = end;
                goto EXIT;
            }

            gl.nr++;
            gl.line = ss;
            gl.match_start = gl.match_end = gl.len = line_end - ss;
            gl.is_ctxline = true;
            if (!g->callback(&gl))
                goto EXIT;

            last_hit = line_end;
        }

        s = line_end + 1;
    }

EXIT:
    return 0;
}
