#include "STC/include/stc/utf8.h"
#include "pathutil.h"

#include <stdio.h>
#include <string.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#define IMM2_SIZE 2
#define LINK_SIZE 2
#define GET(a,n) (unsigned int)(((a)[n] << 8) | (a)[(n)+1])

#define ECL_MAP     0x01  /* Flag: a 32-byte map is present */

/* Type tags for the items stored in an extended class (OP_ECLASS). These items
follow the OP_ECLASS's flag char and bitmap, and represent a Reverse Polish
Notation list of operands and operators manipulating a stack of bits. */

#define ECL_AND     1 /* Pop two from the stack, AND, and push result. */
#define ECL_OR      2 /* Pop two from the stack, OR, and push result. */
#define ECL_XOR     3 /* Pop two from the stack, XOR, and push result. */
#define ECL_NOT     4 /* Pop one from the stack, NOT, and push result. */
#define ECL_XCLASS  5 /* XCLASS nested within ECLASS; match and push result. */
#define ECL_ANY     6 /* Temporary, only used during compilation. */
#define ECL_NONE    7 /* Temporary, only used during compilation. */

#define OP_LENGTHS \
  1,                             /* End                                    */ \
  1, 1, 1, 1, 1,                 /* \A, \G, \K, \B, \b                     */ \
  1, 1, 1, 1, 1, 1,              /* \D, \d, \S, \s, \W, \w                 */ \
  1, 1, 1,                       /* Any, AllAny, Anybyte                   */ \
  3, 3,                          /* \P, \p                                 */ \
  1, 1, 1, 1, 1,                 /* \R, \H, \h, \V, \v                     */ \
  1,                             /* \X                                     */ \
  1, 1, 1, 1, 1, 1,              /* \Z, \z, $, $M ^, ^M                    */ \
  2,                             /* Char  - the minimum length             */ \
  2,                             /* Chari  - the minimum length            */ \
  2,                             /* not                                    */ \
  2,                             /* noti                                   */ \
  /* Positive single-char repeats                             ** These are */ \
  2, 2, 2, 2, 2, 2,              /* *, *?, +, +?, ?, ??       ** minima in */ \
  2+IMM2_SIZE, 2+IMM2_SIZE,      /* upto, minupto             ** mode      */ \
  2+IMM2_SIZE,                   /* exact                                  */ \
  2, 2, 2, 2+IMM2_SIZE,          /* *+, ++, ?+, upto+                      */ \
  2, 2, 2, 2, 2, 2,              /* *I, *?I, +I, +?I, ?I, ??I ** UTF-8     */ \
  2+IMM2_SIZE, 2+IMM2_SIZE,      /* upto I, minupto I                      */ \
  2+IMM2_SIZE,                   /* exact I                                */ \
  2, 2, 2, 2+IMM2_SIZE,          /* *+I, ++I, ?+I, upto+I                  */ \
  /* Negative single-char repeats - only for chars < 256                   */ \
  2, 2, 2, 2, 2, 2,              /* NOT *, *?, +, +?, ?, ??                */ \
  2+IMM2_SIZE, 2+IMM2_SIZE,      /* NOT upto, minupto                      */ \
  2+IMM2_SIZE,                   /* NOT exact                              */ \
  2, 2, 2, 2+IMM2_SIZE,          /* Possessive NOT *, +, ?, upto           */ \
  2, 2, 2, 2, 2, 2,              /* NOT *I, *?I, +I, +?I, ?I, ??I          */ \
  2+IMM2_SIZE, 2+IMM2_SIZE,      /* NOT upto I, minupto I                  */ \
  2+IMM2_SIZE,                   /* NOT exact I                            */ \
  2, 2, 2, 2+IMM2_SIZE,          /* Possessive NOT *I, +I, ?I, upto I      */ \
  /* Positive type repeats                                                 */ \
  2, 2, 2, 2, 2, 2,              /* Type *, *?, +, +?, ?, ??               */ \
  2+IMM2_SIZE, 2+IMM2_SIZE,      /* Type upto, minupto                     */ \
  2+IMM2_SIZE,                   /* Type exact                             */ \
  2, 2, 2, 2+IMM2_SIZE,          /* Possessive *+, ++, ?+, upto+           */ \
  /* Character class & ref repeats                                         */ \
  1, 1, 1, 1, 1, 1,              /* *, *?, +, +?, ?, ??                    */ \
  1+2*IMM2_SIZE, 1+2*IMM2_SIZE,  /* CRRANGE, CRMINRANGE                    */ \
  1, 1, 1, 1+2*IMM2_SIZE,        /* Possessive *+, ++, ?+, CRPOSRANGE      */ \
  1+(32),    /* CLASS                                  */ \
  1+(32),    /* NCLASS                                 */ \
  0,                             /* XCLASS - variable length               */ \
  0,                             /* ECLASS - variable length               */ \
  1+IMM2_SIZE,                   /* REF                                    */ \
  1+IMM2_SIZE+1,                 /* REFI                                   */ \
  1+2*IMM2_SIZE,                 /* DNREF                                  */ \
  1+2*IMM2_SIZE+1,               /* DNREFI                                 */ \
  1+LINK_SIZE,                   /* RECURSE                                */ \
  1+2*LINK_SIZE+1,               /* CALLOUT                                */ \
  0,                             /* CALLOUT_STR - variable length          */ \
  1+LINK_SIZE,                   /* Alt                                    */ \
  1+LINK_SIZE,                   /* Ket                                    */ \
  1+LINK_SIZE,                   /* KetRmax                                */ \
  1+LINK_SIZE,                   /* KetRmin                                */ \
  1+LINK_SIZE,                   /* KetRpos                                */ \
  1+IMM2_SIZE,                   /* Reverse                                */ \
  1+2*IMM2_SIZE,                 /* VReverse                               */ \
  1+LINK_SIZE,                   /* Assert                                 */ \
  1+LINK_SIZE,                   /* Assert not                             */ \
  1+LINK_SIZE,                   /* Assert behind                          */ \
  1+LINK_SIZE,                   /* Assert behind not                      */ \
  1+LINK_SIZE,                   /* NA Assert                              */ \
  1+LINK_SIZE,                   /* NA Assert behind                       */ \
  1+LINK_SIZE,                   /* Scan substring                         */ \
  1+LINK_SIZE,                   /* ONCE                                   */ \
  1+LINK_SIZE,                   /* SCRIPT_RUN                             */ \
  1+LINK_SIZE,                   /* BRA                                    */ \
  1+LINK_SIZE,                   /* BRAPOS                                 */ \
  1+LINK_SIZE+IMM2_SIZE,         /* CBRA                                   */ \
  1+LINK_SIZE+IMM2_SIZE,         /* CBRAPOS                                */ \
  1+LINK_SIZE,                   /* COND                                   */ \
  1+LINK_SIZE,                   /* SBRA                                   */ \
  1+LINK_SIZE,                   /* SBRAPOS                                */ \
  1+LINK_SIZE+IMM2_SIZE,         /* SCBRA                                  */ \
  1+LINK_SIZE+IMM2_SIZE,         /* SCBRAPOS                               */ \
  1+LINK_SIZE,                   /* SCOND                                  */ \
  1+IMM2_SIZE, 1+2*IMM2_SIZE,    /* CREF, DNCREF                           */ \
  1+IMM2_SIZE, 1+2*IMM2_SIZE,    /* RREF, DNRREF                           */ \
  1, 1,                          /* FALSE, TRUE                            */ \
  1, 1, 1,                       /* BRAZERO, BRAMINZERO, BRAPOSZERO        */ \
  3, 1, 3,                       /* MARK, PRUNE, PRUNE_ARG                 */ \
  1, 3,                          /* SKIP, SKIP_ARG                         */ \
  1, 3,                          /* THEN, THEN_ARG                         */ \
  1, 3,                          /* COMMIT, COMMIT_ARG                     */ \
  1, 1, 1,                       /* FAIL, ACCEPT, ASSERT_ACCEPT            */ \
  1+IMM2_SIZE, 1,                /* CLOSE, SKIPZERO                        */ \
  1,                             /* DEFINE                                 */ \
  1, 1                           /* \B and \b in UCP mode                  */

const uint8_t OP_lengths[] = { OP_LENGTHS };

enum {
  OP_END,            /* 0 End of pattern */

  /* Values corresponding to backslashed metacharacters */

  OP_SOD,            /* 1 Start of data: \A */
  OP_SOM,            /* 2 Start of match (subject + offset): \G */
  OP_SET_SOM,        /* 3 Set start of match (\K) */
  OP_NOT_WORD_BOUNDARY,  /*  4 \B -- see also OP_NOT_UCP_WORD_BOUNDARY */
  OP_WORD_BOUNDARY,      /*  5 \b -- see also OP_UCP_WORD_BOUNDARY */
  OP_NOT_DIGIT,          /*  6 \D */
  OP_DIGIT,              /*  7 \d */
  OP_NOT_WHITESPACE,     /*  8 \S */
  OP_WHITESPACE,         /*  9 \s */
  OP_NOT_WORDCHAR,       /* 10 \W */
  OP_WORDCHAR,           /* 11 \w */

  OP_ANY,            /* 12 Match any character except newline (\N) */
  OP_ALLANY,         /* 13 Match any character */
  OP_ANYBYTE,        /* 14 Match any byte (\C); different to OP_ANY for UTF-8 */
  OP_NOTPROP,        /* 15 \P (not Unicode property) */
  OP_PROP,           /* 16 \p (Unicode property) */
  OP_ANYNL,          /* 17 \R (any newline sequence) */
  OP_NOT_HSPACE,     /* 18 \H (not horizontal whitespace) */
  OP_HSPACE,         /* 19 \h (horizontal whitespace) */
  OP_NOT_VSPACE,     /* 20 \V (not vertical whitespace) */
  OP_VSPACE,         /* 21 \v (vertical whitespace) */
  OP_EXTUNI,         /* 22 \X (extended Unicode sequence */
  OP_EODN,           /* 23 End of data or \n at end of data (\Z) */
  OP_EOD,            /* 24 End of data (\z) */

  /* Line end assertions */

  OP_DOLL,           /* 25 End of line - not multiline */
  OP_DOLLM,          /* 26 End of line - multiline */
  OP_CIRC,           /* 27 Start of line - not multiline */
  OP_CIRCM,          /* 28 Start of line - multiline */

  /* Single characters; caseful must precede the caseless ones, and these
  must remain in this order, and adjacent. */

  OP_CHAR,           /* 29 Match one character, casefully */
  OP_CHARI,          /* 30 Match one character, caselessly */
  OP_NOT,            /* 31 Match one character, not the given one, casefully */
  OP_NOTI,           /* 32 Match one character, not the given one, caselessly */

  /* The following sets of 13 opcodes must always be kept in step because
  the offset from the first one is used to generate the others. */

  /* Repeated characters; caseful must precede the caseless ones */

  OP_STAR,           /* 33 The maximizing and minimizing versions of */
  OP_MINSTAR,        /* 34 these six opcodes must come in pairs, with */
  OP_PLUS,           /* 35 the minimizing one second. */
  OP_MINPLUS,        /* 36 */
  OP_QUERY,          /* 37 */
  OP_MINQUERY,       /* 38 */

  OP_UPTO,           /* 39 From 0 to n matches of one character, caseful*/
  OP_MINUPTO,        /* 40 */
  OP_EXACT,          /* 41 Exactly n matches */

  OP_POSSTAR,        /* 42 Possessified star, caseful */
  OP_POSPLUS,        /* 43 Possessified plus, caseful */
  OP_POSQUERY,       /* 44 Posesssified query, caseful */
  OP_POSUPTO,        /* 45 Possessified upto, caseful */

  /* Repeated characters; caseless must follow the caseful ones */

  OP_STARI,          /* 46 */
  OP_MINSTARI,       /* 47 */
  OP_PLUSI,          /* 48 */
  OP_MINPLUSI,       /* 49 */
  OP_QUERYI,         /* 50 */
  OP_MINQUERYI,      /* 51 */

  OP_UPTOI,          /* 52 From 0 to n matches of one character, caseless */
  OP_MINUPTOI,       /* 53 */
  OP_EXACTI,         /* 54 */

  OP_POSSTARI,       /* 55 Possessified star, caseless */
  OP_POSPLUSI,       /* 56 Possessified plus, caseless */
  OP_POSQUERYI,      /* 57 Posesssified query, caseless */
  OP_POSUPTOI,       /* 58 Possessified upto, caseless */

  /* The negated ones must follow the non-negated ones, and match them */
  /* Negated repeated character, caseful; must precede the caseless ones */

  OP_NOTSTAR,        /* 59 The maximizing and minimizing versions of */
  OP_NOTMINSTAR,     /* 60 these six opcodes must come in pairs, with */
  OP_NOTPLUS,        /* 61 the minimizing one second. They must be in */
  OP_NOTMINPLUS,     /* 62 exactly the same order as those above. */
  OP_NOTQUERY,       /* 63 */
  OP_NOTMINQUERY,    /* 64 */

  OP_NOTUPTO,        /* 65 From 0 to n matches, caseful */
  OP_NOTMINUPTO,     /* 66 */
  OP_NOTEXACT,       /* 67 Exactly n matches */

  OP_NOTPOSSTAR,     /* 68 Possessified versions, caseful */
  OP_NOTPOSPLUS,     /* 69 */
  OP_NOTPOSQUERY,    /* 70 */
  OP_NOTPOSUPTO,     /* 71 */

  /* Negated repeated character, caseless; must follow the caseful ones */

  OP_NOTSTARI,       /* 72 */
  OP_NOTMINSTARI,    /* 73 */
  OP_NOTPLUSI,       /* 74 */
  OP_NOTMINPLUSI,    /* 75 */
  OP_NOTQUERYI,      /* 76 */
  OP_NOTMINQUERYI,   /* 77 */

  OP_NOTUPTOI,       /* 78 From 0 to n matches, caseless */
  OP_NOTMINUPTOI,    /* 79 */
  OP_NOTEXACTI,      /* 80 Exactly n matches */

  OP_NOTPOSSTARI,    /* 81 Possessified versions, caseless */
  OP_NOTPOSPLUSI,    /* 82 */
  OP_NOTPOSQUERYI,   /* 83 */
  OP_NOTPOSUPTOI,    /* 84 */

  /* Character types */

  OP_TYPESTAR,       /* 85 The maximizing and minimizing versions of */
  OP_TYPEMINSTAR,    /* 86 these six opcodes must come in pairs, with */
  OP_TYPEPLUS,       /* 87 the minimizing one second. These codes must */
  OP_TYPEMINPLUS,    /* 88 be in exactly the same order as those above. */
  OP_TYPEQUERY,      /* 89 */
  OP_TYPEMINQUERY,   /* 90 */

  OP_TYPEUPTO,       /* 91 From 0 to n matches */
  OP_TYPEMINUPTO,    /* 92 */
  OP_TYPEEXACT,      /* 93 Exactly n matches */

  OP_TYPEPOSSTAR,    /* 94 Possessified versions */
  OP_TYPEPOSPLUS,    /* 95 */
  OP_TYPEPOSQUERY,   /* 96 */
  OP_TYPEPOSUPTO,    /* 97 */

  /* These are used for character classes and back references; only the
  first six are the same as the sets above. */

  OP_CRSTAR,         /* 98 The maximizing and minimizing versions of */
  OP_CRMINSTAR,      /* 99 all these opcodes must come in pairs, with */
  OP_CRPLUS,         /* 100 the minimizing one second. These codes must */
  OP_CRMINPLUS,      /* 101 be in exactly the same order as those above. */
  OP_CRQUERY,        /* 102 */
  OP_CRMINQUERY,     /* 103 */

  OP_CRRANGE,        /* 104 These are different to the three sets above. */
  OP_CRMINRANGE,     /* 105 */

  OP_CRPOSSTAR,      /* 106 Possessified versions */
  OP_CRPOSPLUS,      /* 107 */
  OP_CRPOSQUERY,     /* 108 */
  OP_CRPOSRANGE,     /* 109 */

  /* End of quantifier opcodes */

  OP_CLASS,          /* 110 Match a character class, chars < 256 only */
  OP_NCLASS,         /* 111 Same, but the bitmap was created from a negative
                              class - the difference is relevant only when a
                              character > 255 is encountered. */
  OP_XCLASS,         /* 112 Extended class for handling > 255 chars within the
                              class. This does both positive and negative. */
  OP_ECLASS,         /* 113 Really-extended class, for handling logical
                              expressions computed over characters. */
  OP_REF,            /* 114 Match a back reference, casefully */
  OP_REFI,           /* 115 Match a back reference, caselessly */
  OP_DNREF,          /* 116 Match a duplicate name backref, casefully */
  OP_DNREFI,         /* 117 Match a duplicate name backref, caselessly */
  OP_RECURSE,        /* 118 Match a numbered subpattern (possibly recursive) */
  OP_CALLOUT,        /* 119 Call out to external function if provided */
  OP_CALLOUT_STR,    /* 120 Call out with string argument */

  OP_ALT,            /* 121 Start of alternation */
  OP_KET,            /* 122 End of group that doesn't have an unbounded repeat */
  OP_KETRMAX,        /* 123 These two must remain together and in this */
  OP_KETRMIN,        /* 124 order. They are for groups the repeat for ever. */
  OP_KETRPOS,        /* 125 Possessive unlimited repeat. */

  /* The assertions must come before BRA, CBRA, ONCE, and COND. */

  OP_REVERSE,        /* 126 Move pointer back - used in lookbehind assertions */
  OP_VREVERSE,       /* 127 Move pointer back - variable */
  OP_ASSERT,         /* 128 Positive lookahead */
  OP_ASSERT_NOT,     /* 129 Negative lookahead */
  OP_ASSERTBACK,     /* 130 Positive lookbehind */
  OP_ASSERTBACK_NOT, /* 131 Negative lookbehind */
  OP_ASSERT_NA,      /* 132 Positive non-atomic lookahead */
  OP_ASSERTBACK_NA,  /* 133 Positive non-atomic lookbehind */
  OP_ASSERT_SCS,     /* 134 Scan substring */

  /* ONCE, SCRIPT_RUN, BRA, BRAPOS, CBRA, CBRAPOS, and COND must come
  immediately after the assertions, with ONCE first, as there's a test for >=
  ONCE for a subpattern that isn't an assertion. The POS versions must
  immediately follow the non-POS versions in each case. */

  OP_ONCE,           /* 135 Atomic group, contains captures */
  OP_SCRIPT_RUN,     /* 136 Non-capture, but check characters' scripts */
  OP_BRA,            /* 137 Start of non-capturing bracket */
  OP_BRAPOS,         /* 138 Ditto, with unlimited, possessive repeat */
  OP_CBRA,           /* 139 Start of capturing bracket */
  OP_CBRAPOS,        /* 140 Ditto, with unlimited, possessive repeat */
  OP_COND,           /* 141 Conditional group */

  /* These five must follow the previous five, in the same order. There's a
  check for >= SBRA to distinguish the two sets. */

  OP_SBRA,           /* 142 Start of non-capturing bracket, check empty  */
  OP_SBRAPOS,        /* 143 Ditto, with unlimited, possessive repeat */
  OP_SCBRA,          /* 144 Start of capturing bracket, check empty */
  OP_SCBRAPOS,       /* 145 Ditto, with unlimited, possessive repeat */
  OP_SCOND,          /* 146 Conditional group, check empty */

  /* The next two pairs must (respectively) be kept together. */

  OP_CREF,           /* 147 Used to hold a capture number as condition */
  OP_DNCREF,         /* 148 Used to point to duplicate names as a condition */
  OP_RREF,           /* 149 Used to hold a recursion number as condition */
  OP_DNRREF,         /* 150 Used to point to duplicate names as a condition */
  OP_FALSE,          /* 151 Always false (used by DEFINE and VERSION) */
  OP_TRUE,           /* 152 Always true (used by VERSION) */

  OP_BRAZERO,        /* 153 These two must remain together and in this */
  OP_BRAMINZERO,     /* 154 order. */
  OP_BRAPOSZERO,     /* 155 */

  /* These are backtracking control verbs */

  OP_MARK,           /* 156 always has an argument */
  OP_PRUNE,          /* 157 */
  OP_PRUNE_ARG,      /* 158 same, but with argument */
  OP_SKIP,           /* 159 */
  OP_SKIP_ARG,       /* 160 same, but with argument */
  OP_THEN,           /* 161 */
  OP_THEN_ARG,       /* 162 same, but with argument */
  OP_COMMIT,         /* 163 */
  OP_COMMIT_ARG,     /* 164 same, but with argument */

  /* These are forced failure and success verbs. FAIL and ACCEPT do accept an
  argument, but these cases can be compiled as, for example, (*MARK:X)(*FAIL)
  without the need for a special opcode. */

  OP_FAIL,           /* 165 */
  OP_ACCEPT,         /* 166 */
  OP_ASSERT_ACCEPT,  /* 167 Used inside assertions */
  OP_CLOSE,          /* 168 Used before OP_ACCEPT to close open captures */

  /* This is used to skip a subpattern with a {0} quantifier */

  OP_SKIPZERO,       /* 169 */

  /* This is used to identify a DEFINE group during compilation so that it can
  be checked for having only one branch. It is changed to OP_FALSE before
  compilation finishes. */

  OP_DEFINE,         /* 170 */

  /* These opcodes replace their normal counterparts in UCP mode when
  PCRE2_EXTRA_ASCII_BSW is not set. */

  OP_NOT_UCP_WORD_BOUNDARY, /* 171 */
  OP_UCP_WORD_BOUNDARY,     /* 172 */

  /* This is not an opcode, but is used to check that tables indexed by opcode
  are the correct length, in order to catch updating errors - there have been
  some in the past. */

  OP_TABLE_LENGTH

};

typedef struct pcre2_memctl {
    void *    (*malloc)(size_t, void *);
    void      (*free)(void *, void *);
    void      *memory_data;
} pcre2_memctl;

typedef struct pcre2_real_code {
    pcre2_memctl memctl;            /* Memory control fields */
    const uint8_t *tables;          /* The character tables */
    void    *executable_jit;        /* Pointer to JIT code */
    uint8_t  start_bitmap[32];      /* Bitmap for starting code unit < 256 */
    size_t blocksize;               /* Total (bytes) that was malloc-ed */
    size_t code_start;              /* Byte code start offset */
    uint32_t magic_number;          /* Paranoid and endianness check */
    uint32_t compile_options;       /* Options passed to pcre2_compile() */
    uint32_t overall_options;       /* Options after processing the pattern */
    uint32_t extra_options;         /* Taken from compile_context */
    uint32_t flags;                 /* Various state flags */
    uint32_t limit_heap;            /* Limit set in the pattern */
    uint32_t limit_match;           /* Limit set in the pattern */
    uint32_t limit_depth;           /* Limit set in the pattern */
    uint32_t first_codeunit;        /* Starting code unit */
    uint32_t last_codeunit;         /* This codeunit must be seen */
    uint16_t bsr_convention;        /* What \R matches */
    uint16_t newline_convention;    /* What is a newline? */
    uint16_t max_lookbehind;        /* Longest lookbehind (characters) */
    uint16_t minlength;             /* Minimum length of match */
    uint16_t top_bracket;           /* Highest numbered group */
    uint16_t top_backref;           /* Highest numbered back reference */
    uint16_t name_entry_size;       /* Size (code units) of table entries */
    uint16_t name_count;            /* Number of name entries in the table */
    uint32_t optimization_flags;    /* Optimizations enabled at compile time */
} pcre2_real_code;

int char_width(PCRE2_SPTR data, bool utf)
{
    if (!utf)
        return 0;
    utf8_decode_t d = {.state=0};
    int n = utf8_decode_codepoint(&d, (const char *)data, NULL);
    return n - 1;
}

int ccode_width(int c)
{
    switch(c) {
        case OP_CRSTAR:
        case OP_CRMINSTAR:
        case OP_CRPLUS:
        case OP_CRMINPLUS:
        case OP_CRQUERY:
        case OP_CRMINQUERY:
        case OP_CRPOSSTAR:
        case OP_CRPOSPLUS:
        case OP_CRPOSQUERY:
        case OP_CRRANGE:
        case OP_CRMINRANGE:
        case OP_CRPOSRANGE:
            return OP_lengths[c];
    }
    return 0;
}

static PCRE2_SPTR next_token(PCRE2_SPTR code, bool utf, int *op, char *out, int *len, int *skips)
{
    for ( ; ; (*skips)++) {
        PCRE2_SPTR ccode;
        uint32_t c;
        unsigned int extra = 0;

        // printf("a=%d, depth=%d\n", *code, depth);
        switch (*code) {
            case OP_CHAR:
            case OP_CHARI:
                *len = 0;
                *op = *code;
                do {
                    code++;
                    int n = char_width(code, utf) + 1;
                    memcpy(out + *len, code, n);
                    *len += n;
                    code += n;
                } while (*code == *op);
                return code;

            case OP_END:
            case OP_BRA:
            case OP_BRAPOS:
            case OP_CBRA:
            case OP_CBRAPOS:
            case OP_COND:
            case OP_SBRA:
            case OP_SBRAPOS:
            case OP_SCBRA:
            case OP_SCBRAPOS:
            case OP_SCOND:
            case OP_BRAZERO:
            case OP_BRAMINZERO:
            case OP_BRAPOSZERO:
            case OP_ALT:
            case OP_KET:
            case OP_KETRMAX:
            case OP_KETRMIN:
            case OP_KETRPOS:
                *op = *code;
                return code + OP_lengths[*code] + extra;

            case OP_POSSTARI:
            case OP_PLUSI:
            case OP_MINPLUSI:
            case OP_POSPLUSI:
            case OP_QUERYI:
            case OP_MINQUERYI:
            case OP_POSQUERYI:
            case OP_STAR:
            case OP_MINSTAR:
            case OP_POSSTAR:
            case OP_PLUS:
            case OP_MINPLUS:
            case OP_POSPLUS:
            case OP_QUERY:
            case OP_MINQUERY:
            case OP_POSQUERY:
            case OP_NOTI:
            case OP_NOT:
            case OP_NOTSTARI:
            case OP_NOTMINSTARI:
            case OP_NOTPOSSTARI:
            case OP_NOTPLUSI:
            case OP_NOTMINPLUSI:
            case OP_NOTPOSPLUSI:
            case OP_NOTQUERYI:
            case OP_NOTMINQUERYI:
            case OP_NOTPOSQUERYI:
            case OP_NOTSTAR:
            case OP_NOTMINSTAR:
            case OP_NOTPOSSTAR:
            case OP_NOTPLUS:
            case OP_NOTMINPLUS:
            case OP_NOTPOSPLUS:
            case OP_NOTQUERY:
            case OP_NOTMINQUERY:
            case OP_NOTPOSQUERY:
                extra = char_width(code + 1, utf);
                break;

            case OP_TYPESTAR:
            case OP_TYPEMINSTAR:
            case OP_TYPEPOSSTAR:
            case OP_TYPEPLUS:
            case OP_TYPEMINPLUS:
            case OP_TYPEPOSPLUS:
            case OP_TYPEQUERY:
            case OP_TYPEMINQUERY:
            case OP_TYPEPOSQUERY:
                if (code[1] == OP_PROP || code[1] == OP_NOTPROP)
                    extra = 2;
                break;

            case OP_EXACTI:
            case OP_UPTOI:
            case OP_MINUPTOI:
            case OP_POSUPTOI:
            case OP_EXACT:
            case OP_UPTO:
            case OP_MINUPTO:
            case OP_POSUPTO:
            case OP_NOTEXACTI:
            case OP_NOTUPTOI:
            case OP_NOTMINUPTOI:
            case OP_NOTPOSUPTOI:
            case OP_NOTEXACT:
            case OP_NOTUPTO:
            case OP_NOTMINUPTO:
            case OP_NOTPOSUPTO:
                extra = char_width(code + 1 + IMM2_SIZE, utf);
                break;

            case OP_TYPEEXACT:
            case OP_TYPEUPTO:
            case OP_TYPEMINUPTO:
            case OP_TYPEPOSUPTO:
                if (code[1 + IMM2_SIZE] == OP_PROP || code[1 + IMM2_SIZE] == OP_NOTPROP)
                    extra = 2;
                break;

            case OP_REFI:
            case OP_REF:
            case OP_DNREFI:
            case OP_DNREF:
                ccode = code + OP_lengths[*code];
                extra += ccode_width(*ccode);
                break;

            case OP_CALLOUT_STR:
                extra = GET(code, 1 + 2*LINK_SIZE);
                break;

            case OP_ECLASS:
                extra = GET(code, 1);
                ccode = code + 1 + LINK_SIZE + 1;
                if ((ccode[-1] & ECL_MAP) != 0)
                    ccode += 32;
                while (ccode < code + extra)
                    ccode += *ccode == ECL_XCLASS ? GET(ccode, 1) : 1;
                extra += ccode_width(*ccode);
                break;

            case OP_XCLASS:
                extra = GET(code, 1);
                // fallthrough
            case OP_CLASS:
            case OP_NCLASS:
                ccode = code + OP_lengths[*code] + extra;
                extra += ccode_width(*ccode);
                break;

            case OP_MARK:
            case OP_COMMIT_ARG:
            case OP_PRUNE_ARG:
            case OP_SKIP_ARG:
            case OP_THEN_ARG:
                extra += code[1];
                break;

            default:
                break;
        }

        code += OP_lengths[*code] + extra;
    }

    return NULL;
}

bool extract_fixed(const char *pattern, pcre2_code *re, vec_cct *fixed) {
    char out[strlen((const char *)pattern)];
    PCRE2_SPTR code = (PCRE2_SPTR)((uint8_t *)re + re->code_start);
    bool utf = (re->overall_options & PCRE2_UTF) != 0;
    int len = 0, op = 0, depth = 0, skips = 0;

    while (1) {
        code = next_token(code, utf, &op, out, &len, &skips);
        if (op == OP_END) 
            break;
        switch (op) {
            case OP_CHAR:
            case OP_CHARI:
                if (depth == 1)
                    vec_cct_push(fixed, strndup(out, len));
                break;
            case OP_ALT:
                if (depth == 1) {
                    // e.g.: pattern = "a|b|c"
                    vec_cct_clear(fixed);
                    return false;
                }
                break;
            case OP_KET:
            case OP_KETRMAX:
            case OP_KETRMIN:
            case OP_KETRPOS:
                depth--;
                break;
            default:
                depth++;
                break;
        }
    }
    return skips == 0;
}

int main(void) {
    // Pattern: one or more word characters
    PCRE2_SPTR pattern = (PCRE2_SPTR)"a\\x20?b";
    PCRE2_SPTR subject = (PCRE2_SPTR)"Hello 123 World";

    int errornumber;
    PCRE2_SIZE erroroffset;
    pcre2_code *re;

    // Compile the regular expression
    re = pcre2_compile(
            pattern,               // pattern
            PCRE2_ZERO_TERMINATED, // pattern length
            PCRE2_UTF,                     // options
            &errornumber,          // for error code
            &erroroffset,          // for error offset
            NULL                   // use default compile context
            );

    vec_cct arr = {0};
    bool fixed = extract_fixed((char *)pattern, re, &arr);
    for (c_each(i, vec_cct, arr)) {
        printf("[%s]\n", *(i.ref));
    }
    printf("fixed-%d\n", fixed);
    vec_cct_drop(&arr);
}
