#include "fixture.h"
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
    DBG("all fixed: %d\n", skips == 0);
    return skips == 0;
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
