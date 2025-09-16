#include "vendor.h"
#include "grepper.h"
#include "stclib/priv/utf8_prv.c"

#define NOTACHAR 0xFFFFFFFF

const uint8_t OP_lengths[] = { OP_LENGTHS };

const uint32_t ucd_caseless_sets[] = {
  NOTACHAR,
  0x0053,  0x0073,  0x017f,  NOTACHAR,
  0x01c4,  0x01c5,  0x01c6,  NOTACHAR,
  0x01c7,  0x01c8,  0x01c9,  NOTACHAR,
  0x01ca,  0x01cb,  0x01cc,  NOTACHAR,
  0x01f1,  0x01f2,  0x01f3,  NOTACHAR,
  0x0345,  0x0399,  0x03b9,  0x1fbe,  NOTACHAR,
  0x00b5,  0x039c,  0x03bc,  NOTACHAR,
  0x03a3,  0x03c2,  0x03c3,  NOTACHAR,
  0x0392,  0x03b2,  0x03d0,  NOTACHAR,
  0x0398,  0x03b8,  0x03d1,  0x03f4,  NOTACHAR,
  0x03a6,  0x03c6,  0x03d5,  NOTACHAR,
  0x03a0,  0x03c0,  0x03d6,  NOTACHAR,
  0x039a,  0x03ba,  0x03f0,  NOTACHAR,
  0x03a1,  0x03c1,  0x03f1,  NOTACHAR,
  0x0395,  0x03b5,  0x03f5,  NOTACHAR,
  0x0412,  0x0432,  0x1c80,  NOTACHAR,
  0x0414,  0x0434,  0x1c81,  NOTACHAR,
  0x041e,  0x043e,  0x1c82,  NOTACHAR,
  0x0421,  0x0441,  0x1c83,  NOTACHAR,
  0x0422,  0x0442,  0x1c84,  0x1c85,  NOTACHAR,
  0x042a,  0x044a,  0x1c86,  NOTACHAR,
  0x0462,  0x0463,  0x1c87,  NOTACHAR,
  0x1e60,  0x1e61,  0x1e9b,  NOTACHAR,
  0x03a9,  0x03c9,  0x2126,  NOTACHAR,
  0x004b,  0x006b,  0x212a,  NOTACHAR,
  0x00c5,  0x00e5,  0x212b,  NOTACHAR,
  0x1c88,  0xa64a,  0xa64b,  NOTACHAR,
  0x0069,  0x0130,  NOTACHAR,
  0x0049,  0x0131,  NOTACHAR,
};


static int char_width(PCRE2_SPTR data, bool utf)
{
    if (!utf)
        return 0;
    utf8_decode_t d = {.state=0};
    int n = utf8_decode_codepoint(&d, (const char *)data, NULL);
    return n - 1;
}

static int ccode_width(int c)
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
    *len = 0;
    while (1) {
        PCRE2_SPTR ccode;
        unsigned int extra = 0;

        DBG("op=%d[%d, %d], outlen=%d\n", *code, code[1], code[2], *len);
        switch (*code) {
            case OP_CHAR:
            case OP_CHARI:
                *op = *code;
                do {
                    code++;
                    int n = char_width(code, utf) + 1;
                    memcpy(out + *len, code, n);
                    *len += n;
                    code += n;
                } while (*code == *op);
                DBG("chars(%d), utf(%d): [[%.*s]]\n", *len, utf, *len, out);
                goto EXIT;

            case OP_PROP:
                if (code[1] == 9) { // PT_CLIST
                    char tmp[4];
                    uint32_t c = ucd_caseless_sets[code[2]];
                    int n = utf8_encode(tmp, c);
                    DBG("PT_CLIST char: 0x%x, len=%d\n", c, n);
                    memcpy(out + *len, tmp, n);
                    *len += n;
                    code += OP_lengths[*code] + extra;
                    goto EXIT;
                }
                break;

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
        (*skips)++;
        continue;

EXIT:
        switch (*code) {
            case OP_CHAR:
            case OP_CHARI:
            case OP_PROP:
                break;
            default:
                return code;
        }
    }

    return NULL;
}

bool extract_fixed(const char *pattern, pcre2_code *re, struct strings *fixed) {
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
                    strings_push(fixed, strndup(out, len));
                break;
            case OP_ALT:
                if (depth == 1) {
                    // e.g.: pattern = "a|b|c"
                    strings_clear(fixed);
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
    bool all_fixed = skips == 0 && fixed->len > 0;
    DBG("all fixed: %d %ld\n", all_fixed, fixed->len);
    return all_fixed;
}

// int main(void) {
//     // Pattern: one or more word characters
//     PCRE2_SPTR pattern = (PCRE2_SPTR)"a\\x20?b";
//     PCRE2_SPTR subject = (PCRE2_SPTR)"Hello 123 World";
// 
//     int errornumber;
//     PCRE2_SIZE erroroffset;
//     pcre2_code *re;
// 
//     // Compile the regular expression
//     re = pcre2_compile(
//             pattern,               // pattern
//             PCRE2_ZERO_TERMINATED, // pattern length
//             PCRE2_UTF,                     // options
//             &errornumber,          // for error code
//             &erroroffset,          // for error offset
//             NULL                   // use default compile context
//             );
// 
//     vec_cct arr = {0};
//     bool fixed = extract_fixed((char *)pattern, re, &arr);
//     for (c_each(i, vec_cct, arr)) {
//         printf("[%s]\n", *(i.ref));
//     }
//     printf("fixed-%d\n", fixed);
//     vec_cct_drop(&arr);
// }

/*
**  Do shell-style pattern matching for ?, \, [], and * characters.
**  It is 8bit clean.
**
**  Written by Rich $alz, mirror!rs, Wed Nov 26 19:03:17 EST 1986.
**  Rich $alz is now <rsalz@bbn.com>.
**
**  Modified by Wayne Davison to special-case '/' matching, to make '**'
**  work differently than '*', and to fix the character-class code.
*/

/*
 * Sane locale-independent, ASCII ctype.
 *
 * No surprises, and works with signed and unsigned chars.
 */

enum {
	S = GIT_SPACE,
	A = GIT_ALPHA,
	D = GIT_DIGIT,
	G = GIT_GLOB_SPECIAL,	/* *, ?, [, \\ */
	R = GIT_REGEX_SPECIAL,	/* $, (, ), +, ., ^, {, | */
	P = GIT_PATHSPEC_MAGIC, /* other non-alnum, except for ] and } */
	X = GIT_CNTRL,
	U = GIT_PUNCT,
	Z = GIT_CNTRL | GIT_SPACE
};

const unsigned char sane_ctype[256] = {
	X, X, X, X, X, X, X, X, X, Z, Z, X, X, Z, X, X,		/*   0.. 15 */
	X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,		/*  16.. 31 */
	S, P, P, P, R, P, P, P, R, R, G, R, P, P, R, P,		/*  32.. 47 */
	D, D, D, D, D, D, D, D, D, D, P, P, P, P, P, G,		/*  48.. 63 */
	P, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,		/*  64.. 79 */
	A, A, A, A, A, A, A, A, A, A, A, G, G, U, R, P,		/*  80.. 95 */
	P, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,		/*  96..111 */
	A, A, A, A, A, A, A, A, A, A, A, R, R, U, P, X,		/* 112..127 */
	/* Nothing in the 128.. range */
};

const signed char hexval_table[256] = {
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 00-07 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 08-0f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 10-17 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 18-1f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 20-27 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 28-2f */
	  0,  1,  2,  3,  4,  5,  6,  7,		/* 30-37 */
	  8,  9, -1, -1, -1, -1, -1, -1,		/* 38-3f */
	 -1, 10, 11, 12, 13, 14, 15, -1,		/* 40-47 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 48-4f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 50-57 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 58-5f */
	 -1, 10, 11, 12, 13, 14, 15, -1,		/* 60-67 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 68-67 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 70-77 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 78-7f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 80-87 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 88-8f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 90-97 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 98-9f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* a0-a7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* a8-af */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* b0-b7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* b8-bf */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* c0-c7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* c8-cf */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* d0-d7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* d8-df */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* e0-e7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* e8-ef */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* f0-f7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* f8-ff */
};

typedef unsigned char uchar;

/* Internal return values */
#define WM_ABORT_ALL -1
#define WM_ABORT_TO_STARSTAR -2

/* What character marks an inverted character class? */
#define NEGATE_CLASS	'!'
#define NEGATE_CLASS2	'^'

#define CC_EQ(class, len, litmatch) ((len) == sizeof (litmatch)-1 \
				    && *(class) == *(litmatch) \
				    && strncmp((char*)class, litmatch, len) == 0)

#if defined STDC_HEADERS || !defined isascii
# define ISASCII(c) 1
#else
# define ISASCII(c) isascii(c)
#endif

#ifdef isblank
# define ISBLANK(c) (ISASCII(c) && isblank(c))
#else
# define ISBLANK(c) ((c) == ' ' || (c) == '\t')
#endif

#ifdef isgraph
# define ISGRAPH(c) (ISASCII(c) && isgraph(c))
#else
# define ISGRAPH(c) (ISASCII(c) && isprint(c) && !isspace(c))
#endif

#define ISPRINT(c) (ISASCII(c) && isprint(c))
#define ISDIGIT(c) (ISASCII(c) && isdigit(c))
#define ISALNUM(c) (ISASCII(c) && isalnum(c))
#define ISALPHA(c) (ISASCII(c) && isalpha(c))
#define ISCNTRL(c) (ISASCII(c) && iscntrl(c))
#define ISLOWER(c) (ISASCII(c) && islower(c))
#define ISPUNCT(c) (ISASCII(c) && ispunct(c))
#define ISSPACE(c) (ISASCII(c) && isspace(c))
#define ISUPPER(c) (ISASCII(c) && isupper(c))
#define ISXDIGIT(c) (ISASCII(c) && isxdigit(c))

/* Match pattern "p" against "text" */
static int dowild(const uchar *p, const uchar *text, unsigned int flags)
{
	uchar p_ch;
	const uchar *pattern = p;

	for ( ; (p_ch = *p) != '\0'; text++, p++) {
		int matched, match_slash, negated;
		uchar t_ch, prev_ch;
		if ((t_ch = *text) == '\0' && p_ch != '*')
			return WM_ABORT_ALL;
		if ((flags & WM_CASEFOLD) && ISUPPER(t_ch))
			t_ch = tolower(t_ch);
		if ((flags & WM_CASEFOLD) && ISUPPER(p_ch))
			p_ch = tolower(p_ch);
		switch (p_ch) {
		case '\\':
			/* Literal match with following character.  Note that the test
			 * in "default" handles the p[1] == '\0' failure case. */
			p_ch = *++p;
			/* FALLTHROUGH */
		default:
			if (t_ch != p_ch)
				return WM_NOMATCH;
			continue;
		case '?':
			/* Match anything but '/'. */
			if ((flags & WM_PATHNAME) && t_ch == '/')
				return WM_NOMATCH;
			continue;
		case '*':
			if (*++p == '*') {
				const uchar *prev_p = p;
				while (*++p == '*') {}
				if (!(flags & WM_PATHNAME))
					/* without WM_PATHNAME, '*' == '**' */
					match_slash = 1;
				else if ((prev_p - pattern < 2 || *(prev_p - 2) == '/') &&
				    (*p == '\0' || *p == '/' ||
				     (p[0] == '\\' && p[1] == '/'))) {
					/*
					 * Assuming we already match 'foo/' and are at
					 * <star star slash>, just assume it matches
					 * nothing and go ahead match the rest of the
					 * pattern with the remaining string. This
					 * helps make foo/<*><*>/bar (<> because
					 * otherwise it breaks C comment syntax) match
					 * both foo/bar and foo/a/bar.
					 */
					if (p[0] == '/' &&
					    dowild(p + 1, text, flags) == WM_MATCH)
						return WM_MATCH;
					match_slash = 1;
				} else /* WM_PATHNAME is set */
					match_slash = 0;
			} else
				/* without WM_PATHNAME, '*' == '**' */
				match_slash = flags & WM_PATHNAME ? 0 : 1;
			if (*p == '\0') {
				/* Trailing "**" matches everything.  Trailing "*" matches
				 * only if there are no more slash characters. */
				if (!match_slash) {
					if (strchr((char *)text, '/'))
						return WM_ABORT_TO_STARSTAR;
				}
				return WM_MATCH;
			} else if (!match_slash && *p == '/') {
				/*
				 * _one_ asterisk followed by a slash
				 * with WM_PATHNAME matches the next
				 * directory
				 */
				const char *slash = strchr((char*)text, '/');
				if (!slash)
					return WM_ABORT_ALL;
				text = (const uchar*)slash;
				/* the slash is consumed by the top-level for loop */
				break;
			}
			while (1) {
				if (t_ch == '\0')
					break;
				/*
				 * Try to advance faster when an asterisk is
				 * followed by a literal. We know in this case
				 * that the string before the literal
				 * must belong to "*".
				 * If match_slash is false, do not look past
				 * the first slash as it cannot belong to '*'.
				 */
				if (!is_glob_special(*p)) {
					p_ch = *p;
					if ((flags & WM_CASEFOLD) && ISUPPER(p_ch))
						p_ch = tolower(p_ch);
					while ((t_ch = *text) != '\0' &&
					       (match_slash || t_ch != '/')) {
						if ((flags & WM_CASEFOLD) && ISUPPER(t_ch))
							t_ch = tolower(t_ch);
						if (t_ch == p_ch)
							break;
						text++;
					}
					if (t_ch != p_ch) {
						if (match_slash)
							return WM_ABORT_ALL;
						else
							return WM_ABORT_TO_STARSTAR;
					}
				}
				if ((matched = dowild(p, text, flags)) != WM_NOMATCH) {
					if (!match_slash || matched != WM_ABORT_TO_STARSTAR)
						return matched;
				} else if (!match_slash && t_ch == '/')
					return WM_ABORT_TO_STARSTAR;
				t_ch = *++text;
			}
			return WM_ABORT_ALL;
		case '[':
			p_ch = *++p;
#ifdef NEGATE_CLASS2
			if (p_ch == NEGATE_CLASS2)
				p_ch = NEGATE_CLASS;
#endif
			/* Assign literal 1/0 because of "matched" comparison. */
			negated = p_ch == NEGATE_CLASS ? 1 : 0;
			if (negated) {
				/* Inverted character class. */
				p_ch = *++p;
			}
			prev_ch = 0;
			matched = 0;
			do {
				if (!p_ch)
					return WM_ABORT_ALL;
				if (p_ch == '\\') {
					p_ch = *++p;
					if (!p_ch)
						return WM_ABORT_ALL;
					if (t_ch == p_ch)
						matched = 1;
				} else if (p_ch == '-' && prev_ch && p[1] && p[1] != ']') {
					p_ch = *++p;
					if (p_ch == '\\') {
						p_ch = *++p;
						if (!p_ch)
							return WM_ABORT_ALL;
					}
					if (t_ch <= p_ch && t_ch >= prev_ch)
						matched = 1;
					else if ((flags & WM_CASEFOLD) && ISLOWER(t_ch)) {
						uchar t_ch_upper = toupper(t_ch);
						if (t_ch_upper <= p_ch && t_ch_upper >= prev_ch)
							matched = 1;
					}
					p_ch = 0; /* This makes "prev_ch" get set to 0. */
				} else if (p_ch == '[' && p[1] == ':') {
					const uchar *s;
					int i;
					for (s = p += 2; (p_ch = *p) && p_ch != ']'; p++) {} /*SHARED ITERATOR*/
					if (!p_ch)
						return WM_ABORT_ALL;
					i = p - s - 1;
					if (i < 0 || p[-1] != ':') {
						/* Didn't find ":]", so treat like a normal set. */
						p = s - 2;
						p_ch = '[';
						if (t_ch == p_ch)
							matched = 1;
						continue;
					}
					if (CC_EQ(s,i, "alnum")) {
						if (ISALNUM(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "alpha")) {
						if (ISALPHA(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "blank")) {
						if (ISBLANK(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "cntrl")) {
						if (ISCNTRL(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "digit")) {
						if (ISDIGIT(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "graph")) {
						if (ISGRAPH(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "lower")) {
						if (ISLOWER(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "print")) {
						if (ISPRINT(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "punct")) {
						if (ISPUNCT(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "space")) {
						if (ISSPACE(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "upper")) {
						if (ISUPPER(t_ch))
							matched = 1;
						else if ((flags & WM_CASEFOLD) && ISLOWER(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "xdigit")) {
						if (ISXDIGIT(t_ch))
							matched = 1;
					} else /* malformed [:class:] string */
						return WM_ABORT_ALL;
					p_ch = 0; /* This makes "prev_ch" get set to 0. */
				} else if (t_ch == p_ch)
					matched = 1;
			} while (prev_ch = p_ch, (p_ch = *++p) != ']');
			if (matched == negated ||
			    ((flags & WM_PATHNAME) && t_ch == '/'))
				return WM_NOMATCH;
			continue;
		}
	}

	return *text ? WM_NOMATCH : WM_MATCH;
}

/* Match the "pattern" against the "text" string. */
int wildmatch(const char *pattern, const char *text, unsigned int flags)
{
	int res = dowild((const uchar*)pattern, (const uchar*)text, flags);
	return res == WM_MATCH ? WM_MATCH : WM_NOMATCH;
}
