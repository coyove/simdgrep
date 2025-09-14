/* MIT License
 *
 * Copyright (c) 2025 Tyge Løvset
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
// IWYU pragma: private, include "stc/utf8.h"
#ifndef STC_UTF8_PRV_H_INCLUDED
#define STC_UTF8_PRV_H_INCLUDED

#include <ctype.h>

// The following functions assume valid utf8 strings:

/* number of bytes in the utf8 codepoint from s */
static inline int utf8_chr_size(const char *s) {
    unsigned b = (uint8_t)*s;
    if (b < 0x80) return 1;
    /*if (b < 0xC2) return 0;*/
    if (b < 0xE0) return 2;
    if (b < 0xF0) return 3;
    /*if (b < 0xF5)*/ return 4;
    /*return 0;*/
}

// ------------------------------------------------------
// Functions below must be linked with utf8_prv.c content
// To call them, either define i_import before including
// one of cstr, csview, zsview, or link with src/libstc.a

/* decode next utf8 codepoint. https://bjoern.hoehrmann.de/utf-8/decoder/dfa */
typedef struct { uint32_t state, codep; } utf8_decode_t;
extern const uint8_t utf8_dtab[]; /* utf8code.c */
#define utf8_ACCEPT 0
#define utf8_REJECT 12

extern int      utf8_encode(char *out, uint32_t c);
extern int      utf8_decode_codepoint(utf8_decode_t* d, const char* s, const char* end);
extern uint32_t utf8_tolower(uint32_t c);
extern uint32_t utf8_toupper(uint32_t c);

static inline uint32_t utf8_decode(utf8_decode_t* d, const uint32_t byte) {
    const uint32_t type = utf8_dtab[byte];
    d->codep = d->state ? (byte & 0x3fu) | (d->codep << 6)
                        : (0xffU >> type) & byte;
    return d->state = utf8_dtab[256 + d->state + type];
}

#define c_lowerbound(T, c, at, less, N, ret) do { \
    int _n = N, _i = 0, _mid = _n/2; \
    T _c = c; \
    while (_n > 0) { \
        if (less(at((_i + _mid)), &_c)) { \
            _i += _mid + 1; \
            _n -= _mid + 1; \
            _mid = _n*7/8; \
        } else { \
            _n = _mid; \
            _mid = _n/8; \
        } \
    } \
    *(ret) = _i; \
} while (0)

#endif // STC_UTF8_PRV_H_INCLUDED
