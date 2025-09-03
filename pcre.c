#include <pcre.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "STC/include/stc/utf8.h"

typedef struct real_pcre8_or_16 {
  uint32_t magic_number;
  uint32_t size;               /* Total that was malloced */
  uint32_t options;            /* Public options */
  uint32_t flags;              /* Private flags */
  uint32_t limit_match;        /* Limit set from regex */
  uint32_t limit_recursion;    /* Limit set from regex */
  uint16_t first_char;         /* Starting character */
  uint16_t req_char;           /* This character must be seen */
  uint16_t max_lookbehind;     /* Longest lookbehind (characters) */
  uint16_t top_bracket;        /* Highest numbered group */
  uint16_t top_backref;        /* Highest numbered back reference */
  uint16_t name_table_offset;  /* Offset to name table that follows */
  uint16_t name_entry_size;    /* Size of any name items */
  uint16_t name_count;         /* Number of name items */
  uint16_t ref_count;          /* Reference count */
  uint16_t dummy1;             /* To ensure size is a multiple of 8 */
  uint16_t dummy2;             /* To ensure size is a multiple of 8 */
  uint16_t dummy3;             /* To ensure size is a multiple of 8 */
  const uint8_t *tables;       /* Pointer to tables or NULL for std */
  void          *nullpad;      /* NULL padding */
} real_pcre8_or_16;

#define IMM2_SIZE 2
#define LINK_SIZE 2
#define GET(a,n) (((a)[n] << 8) | (a)[(n)+1])
#define XCL_NOT       0x01    /* Flag: this is a negative class */
#define XCL_MAP       0x02    /* Flag: a 32-byte map is present */
#define XCL_HASPROP   0x04    /* Flag: property checks are present. */

#define XCL_END       0    /* Marks end of individual items */
#define XCL_SINGLE    1    /* Single item (one multibyte char) follows */
#define XCL_RANGE     2    /* A range (two multibyte chars) follows */
#define XCL_PROP      3    /* Unicode property (2-byte property code follows) */
#define XCL_NOTPROP   4    /* Unicode inverted property (ditto) */

enum {
  OP_END,            /* 0 End of pattern */

  /* Values corresponding to backslashed metacharacters */

  OP_SOD,            /* 1 Start of data: \A */
  OP_SOM,            /* 2 Start of match (subject + offset): \G */
  OP_SET_SOM,        /* 3 Set start of match (\K) */
  OP_NOT_WORD_BOUNDARY,  /*  4 \B */
  OP_WORD_BOUNDARY,      /*  5 \b */
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

  /* Single characters; caseful must precede the caseless ones */

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
  OP_REF,            /* 113 Match a back reference, casefully */
  OP_REFI,           /* 114 Match a back reference, caselessly */
  OP_DNREF,          /* 115 Match a duplicate name backref, casefully */
  OP_DNREFI,         /* 116 Match a duplicate name backref, caselessly */
  OP_RECURSE,        /* 117 Match a numbered subpattern (possibly recursive) */
  OP_CALLOUT,        /* 118 Call out to external function if provided */

  OP_ALT,            /* 119 Start of alternation */
  OP_KET,            /* 120 End of group that doesn't have an unbounded repeat */
  OP_KETRMAX,        /* 121 These two must remain together and in this */
  OP_KETRMIN,        /* 122 order. They are for groups the repeat for ever. */
  OP_KETRPOS,        /* 123 Possessive unlimited repeat. */

  /* The assertions must come before BRA, CBRA, ONCE, and COND, and the four
  asserts must remain in order. */

  OP_REVERSE,        /* 124 Move pointer back - used in lookbehind assertions */
  OP_ASSERT,         /* 125 Positive lookahead */
  OP_ASSERT_NOT,     /* 126 Negative lookahead */
  OP_ASSERTBACK,     /* 127 Positive lookbehind */
  OP_ASSERTBACK_NOT, /* 128 Negative lookbehind */

  /* ONCE, ONCE_NC, BRA, BRAPOS, CBRA, CBRAPOS, and COND must come immediately
  after the assertions, with ONCE first, as there's a test for >= ONCE for a
  subpattern that isn't an assertion. The POS versions must immediately follow
  the non-POS versions in each case. */

  OP_ONCE,           /* 129 Atomic group, contains captures */
  OP_ONCE_NC,        /* 130 Atomic group containing no captures */
  OP_BRA,            /* 131 Start of non-capturing bracket */
  OP_BRAPOS,         /* 132 Ditto, with unlimited, possessive repeat */
  OP_CBRA,           /* 133 Start of capturing bracket */
  OP_CBRAPOS,        /* 134 Ditto, with unlimited, possessive repeat */
  OP_COND,           /* 135 Conditional group */

  /* These five must follow the previous five, in the same order. There's a
  check for >= SBRA to distinguish the two sets. */

  OP_SBRA,           /* 136 Start of non-capturing bracket, check empty  */
  OP_SBRAPOS,        /* 137 Ditto, with unlimited, possessive repeat */
  OP_SCBRA,          /* 138 Start of capturing bracket, check empty */
  OP_SCBRAPOS,       /* 139 Ditto, with unlimited, possessive repeat */
  OP_SCOND,          /* 140 Conditional group, check empty */

  /* The next two pairs must (respectively) be kept together. */

  OP_CREF,           /* 141 Used to hold a capture number as condition */
  OP_DNCREF,         /* 142 Used to point to duplicate names as a condition */
  OP_RREF,           /* 143 Used to hold a recursion number as condition */
  OP_DNRREF,         /* 144 Used to point to duplicate names as a condition */
  OP_DEF,            /* 145 The DEFINE condition */

  OP_BRAZERO,        /* 146 These two must remain together and in this */
  OP_BRAMINZERO,     /* 147 order. */
  OP_BRAPOSZERO,     /* 148 */

  /* These are backtracking control verbs */

  OP_MARK,           /* 149 always has an argument */
  OP_PRUNE,          /* 150 */
  OP_PRUNE_ARG,      /* 151 same, but with argument */
  OP_SKIP,           /* 152 */
  OP_SKIP_ARG,       /* 153 same, but with argument */
  OP_THEN,           /* 154 */
  OP_THEN_ARG,       /* 155 same, but with argument */
  OP_COMMIT,         /* 156 */

  /* These are forced failure and success verbs */

  OP_FAIL,           /* 157 */
  OP_ACCEPT,         /* 158 */
  OP_ASSERT_ACCEPT,  /* 159 Used inside assertions */
  OP_CLOSE,          /* 160 Used before OP_ACCEPT to close open captures */

  /* This is used to skip a subpattern with a {0} quantifier */

  OP_SKIPZERO,       /* 161 */

  /* This is not an opcode, but is used to check that tables indexed by opcode
  are the correct length, in order to catch updating errors - there have been
  some in the past. */

  OP_TABLE_LENGTH
};

static const uint8_t priv_OP_lengths[] = {
  1,                             /* End                                    */ 
  1, 1, 1, 1, 1,                 /* \A, \G, \K, \B, \b                     */ 
  1, 1, 1, 1, 1, 1,              /* \D, \d, \S, \s, \W, \w                 */ 
  1, 1, 1,                       /* Any, AllAny, Anybyte                   */ 
  3, 3,                          /* \P, \p                                 */ 
  1, 1, 1, 1, 1,                 /* \R, \H, \h, \V, \v                     */ 
  1,                             /* \X                                     */ 
  1, 1, 1, 1, 1, 1,              /* \Z, \z, $, $M ^, ^M                    */ 
  2,                             /* Char  - the minimum length             */ 
  2,                             /* Chari  - the minimum length            */ 
  2,                             /* not                                    */ 
  2,                             /* noti                                   */ 
  /* Positive single-char repeats                             ** These are */ 
  2, 2, 2, 2, 2, 2,              /* *, *?, +, +?, ?, ??       ** minima in */ 
  2+IMM2_SIZE, 2+IMM2_SIZE,      /* upto, minupto             ** mode      */ 
  2+IMM2_SIZE,                   /* exact                                  */ 
  2, 2, 2, 2+IMM2_SIZE,          /* *+, ++, ?+, upto+                      */ 
  2, 2, 2, 2, 2, 2,              /* *I, *?I, +I, +?I, ?I, ??I ** UTF-8     */ 
  2+IMM2_SIZE, 2+IMM2_SIZE,      /* upto I, minupto I                      */ 
  2+IMM2_SIZE,                   /* exact I                                */ 
  2, 2, 2, 2+IMM2_SIZE,          /* *+I, ++I, ?+I, upto+I                  */ 
  /* Negative single-char repeats - only for chars < 256                   */ 
  2, 2, 2, 2, 2, 2,              /* NOT *, *?, +, +?, ?, ??                */ 
  2+IMM2_SIZE, 2+IMM2_SIZE,      /* NOT upto, minupto                      */ 
  2+IMM2_SIZE,                   /* NOT exact                              */ 
  2, 2, 2, 2+IMM2_SIZE,          /* Possessive NOT *, +, ?, upto           */ 
  2, 2, 2, 2, 2, 2,              /* NOT *I, *?I, +I, +?I, ?I, ??I          */ 
  2+IMM2_SIZE, 2+IMM2_SIZE,      /* NOT upto I, minupto I                  */ 
  2+IMM2_SIZE,                   /* NOT exact I                            */ 
  2, 2, 2, 2+IMM2_SIZE,          /* Possessive NOT *I, +I, ?I, upto I      */ 
  /* Positive type repeats                                                 */ 
  2, 2, 2, 2, 2, 2,              /* Type *, *?, +, +?, ?, ??               */ 
  2+IMM2_SIZE, 2+IMM2_SIZE,      /* Type upto, minupto                     */ 
  2+IMM2_SIZE,                   /* Type exact                             */ 
  2, 2, 2, 2+IMM2_SIZE,          /* Possessive *+, ++, ?+, upto+           */ 
  /* Character class & ref repeats                                         */ 
  1, 1, 1, 1, 1, 1,              /* *, *?, +, +?, ?, ??                    */ 
  1+2*IMM2_SIZE, 1+2*IMM2_SIZE,  /* CRRANGE, CRMINRANGE                    */ 
  1, 1, 1, 1+2*IMM2_SIZE,        /* Possessive *+, ++, ?+, CRPOSRANGE      */ 
  1+(32/1),     /* CLASS                                  */ 
  1+(32/1),     /* NCLASS                                 */ 
  0,                             /* XCLASS - variable length               */ 
  1+IMM2_SIZE,                   /* REF                                    */ 
  1+IMM2_SIZE,                   /* REFI                                   */ 
  1+2*IMM2_SIZE,                 /* DNREF                                  */ 
  1+2*IMM2_SIZE,                 /* DNREFI                                 */ 
  1+LINK_SIZE,                   /* RECURSE                                */ 
  2+2*LINK_SIZE,                 /* CALLOUT                                */ 
  1+LINK_SIZE,                   /* Alt                                    */ 
  1+LINK_SIZE,                   /* Ket                                    */ 
  1+LINK_SIZE,                   /* KetRmax                                */ 
  1+LINK_SIZE,                   /* KetRmin                                */ 
  1+LINK_SIZE,                   /* KetRpos                                */ 
  1+LINK_SIZE,                   /* Reverse                                */ 
  1+LINK_SIZE,                   /* Assert                                 */ 
  1+LINK_SIZE,                   /* Assert not                             */ 
  1+LINK_SIZE,                   /* Assert behind                          */ 
  1+LINK_SIZE,                   /* Assert behind not                      */ 
  1+LINK_SIZE,                   /* ONCE                                   */ 
  1+LINK_SIZE,                   /* ONCE_NC                                */ 
  1+LINK_SIZE,                   /* BRA                                    */ 
  1+LINK_SIZE,                   /* BRAPOS                                 */ 
  1+LINK_SIZE+IMM2_SIZE,         /* CBRA                                   */ 
  1+LINK_SIZE+IMM2_SIZE,         /* CBRAPOS                                */ 
  1+LINK_SIZE,                   /* COND                                   */ 
  1+LINK_SIZE,                   /* SBRA                                   */ 
  1+LINK_SIZE,                   /* SBRAPOS                                */ 
  1+LINK_SIZE+IMM2_SIZE,         /* SCBRA                                  */ 
  1+LINK_SIZE+IMM2_SIZE,         /* SCBRAPOS                               */ 
  1+LINK_SIZE,                   /* SCOND                                  */ 
  1+IMM2_SIZE, 1+2*IMM2_SIZE,    /* CREF, DNCREF                           */ 
  1+IMM2_SIZE, 1+2*IMM2_SIZE,    /* RREF, DNRREF                           */ 
  1,                             /* DEF                                    */ 
  1, 1, 1,                       /* BRAZERO, BRAMINZERO, BRAPOSZERO        */ 
  3, 1, 3,                       /* MARK, PRUNE, PRUNE_ARG                 */ 
  1, 3,                          /* SKIP, SKIP_ARG                         */ 
  1, 3,                          /* THEN, THEN_ARG                         */ 
  1, 1, 1, 1,                    /* COMMIT, FAIL, ACCEPT, ASSERT_ACCEPT    */ 
  1+IMM2_SIZE, 1                 /* CLOSE, SKIPZERO                        */
};

static unsigned int print_char(const uint8_t *ptr) {
    utf8_decode_t d = {.state=0};
    int n = utf8_decode_codepoint(&d, (const char *)ptr, NULL);
    printf("n=%d\n", n);
    return n - 1;
}

int main(void) {
    const char *error;
    int erroffset;
    int ovector[30];   // output vector for substring matches
    int rc;

    const char *pattern = "([a-z]+)zz b这种1 (\\d+)";
    const char *subject = "apple 123";

    // Compile the regex
    pcre *re = pcre_compile(
        pattern,            // the pattern
        PCRE_UTF8,                  // options (0 = default)
        &error,             // error message (if compilation fails)
        &erroffset,         // error offset in pattern
        NULL                // use default character tables
    );

    if (re == NULL) {
        printf("PCRE compilation failed at offset %d: %s\n", erroffset, error);
        return 1;
    }

    const uint8_t *codestart, *code;
    int offset = re->name_table_offset;
    int count = re->name_count;
    int size = re->name_entry_size;
    code = codestart = (const uint8_t *)re + offset + count * size;

    for(;;) {
        const uint8_t *ccode;
        unsigned int extra = 0;

        printf("%d > %d\n", *code, code - codestart);
        switch(*code) {
            case OP_END:
                printf("exit");
                exit(1);

            case OP_CHAR:
                do
                {
                    code++;
                    code += 1 + print_char(code);
                }
                while (*code == OP_CHAR);
                continue;

            case OP_CHARI:
                do
                {
                    code++;
                    code += 1 + print_char(code);
                }
                while (*code == OP_CHARI);
                continue;

            case OP_REFI:
            case OP_REF:
            case OP_DNREFI:
            case OP_DNREF:
                ccode = code + priv_OP_lengths[*code];
                goto CLASS_REF_REPEAT;

            case OP_CLASS:
            case OP_NCLASS:
            case OP_XCLASS:
                {
                    if (*code == OP_XCLASS) {
                        extra = GET(code, 1);
                        ccode = code + LINK_SIZE + 1;
                        if ((*ccode & XCL_MAP) != 0)
                            ccode += 32 / 1;
                        ccode++;
                    } else {
                        ccode = code + 1 + 32 / 1;
                    }

                    /* For an XCLASS there is always some additional data */
                    if (*code == OP_XCLASS) {
                        uint8_t ch;
                        while ((ch = *ccode++) != XCL_END) {
                            switch(ch) {
                                case XCL_NOTPROP:
                                case XCL_PROP:
                                    ccode += 2;
                                    break;

                                default:
                                    ccode += 1 + print_char(ccode);
                                    if (ch == XCL_RANGE) {
                                        ccode += 1 + print_char(ccode);
                                    }
                                    break;
                            }
                        }
                    }

                    /* Handle repeats after a class or a back reference */

CLASS_REF_REPEAT:
                    switch(*ccode)
                    {
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
                            extra += priv_OP_lengths[*ccode];
                            break;
                        default:
                            break;
                    }
                }
                break;

            case OP_MARK:
            case OP_PRUNE_ARG:
            case OP_SKIP_ARG:
            case OP_THEN_ARG:
                extra += code[1];
                break;

            default:
                break;
        }

        code += priv_OP_lengths[*code] + extra;
    }

    // Execute the regex
    rc = pcre_exec(
        re,                 // compiled pattern
        NULL,               // extra data (NULL if not studying)
        subject,            // subject string
        (int)strlen(subject), // length of subject
        0,                  // start offset
        0,                  // options
        ovector,            // output vector for substring offsets
        30                  // size of output vector
    );

    if (rc < 0) {
        switch(rc) {
            case PCRE_ERROR_NOMATCH: printf("No match\n"); break;
            default: printf("Matching error %d\n", rc); break;
        }
        pcre_free(re);
        return 1;
    }

    printf("Match succeeded, %d captures.\n", rc);

    // Print all captured substrings
    for (int i = 0; i < rc; i++) {
        const char *substring;
        pcre_get_substring(subject, ovector, rc, i, &substring);
        printf("Group %d: %s\n", i, substring);
        pcre_free_substring(substring);
    }

    // Free the compiled regex
    pcre_free(re);

    return 0;
}
