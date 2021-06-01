#include "mime_chars.h"

static unsigned classes[256];

/** inicializa la caracterización de cada byte */
const unsigned *
init_char_class(void) {
    for(unsigned i = 0; i < 0xff; i++) {
        unsigned class = 0x00;

        if(i <= 127) {
            class |= TOKEN_CHAR;
        }

        // 'A' - 'Z'
        if(i >= 65 && i <= 90) {
            class |= TOKEN_ALPHA
                  |  TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS
                  |  TOKEN_REST_NAME_FIRST
                  |  TOKEN_REST_NAME_CHARS
                  ;
        }
        // 'a' - 'z'
        if(i >= 97 && i <= 122) {
            class |= TOKEN_ALPHA
                  |  TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS
                  |  TOKEN_REST_NAME_FIRST
                  |  TOKEN_REST_NAME_CHARS
                  ;
        }

        // '0' - '9'
        if(i >= 48 && i <= 57) {
            class |= TOKEN_DIGIT
                  |  TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS
                  |  TOKEN_REST_NAME_FIRST
                  |  TOKEN_REST_NAME_CHARS
                  ;
        }
        if(i <= 31) {
            class |= TOKEN_CTL;
        }

        classes[i] = class;
    }

    /** LWSP-char   =  SPACE / HTAB                  ; semantics = SPACE */
    classes['\t'] |= TOKEN_LWSP;
    classes[' ']  |= TOKEN_LWSP;

    /*
     * specials    =  "(" / ")" / "<" / ">" / "@"    ; Must be in quoted-
     *               /  "," / ";" / ":" / "\" / <">  ;  string, to use
     *               /  "." / "[" / "]"              ;  within a word.
     */
    classes['(']  |= TOKEN_SPECIAL;
    classes[')']  |= TOKEN_SPECIAL;
    classes['<']  |= TOKEN_SPECIAL;
    classes['>']  |= TOKEN_SPECIAL;
    classes['@']  |= TOKEN_SPECIAL;
    classes[',']  |= TOKEN_SPECIAL;
    classes[';']  |= TOKEN_SPECIAL;
    classes[':']  |= TOKEN_SPECIAL;
    classes['\\'] |= TOKEN_SPECIAL;
    classes['"']  |= TOKEN_SPECIAL;
    classes['.']  |= TOKEN_SPECIAL;
    classes[']']  |= TOKEN_SPECIAL;
    classes['[']  |= TOKEN_SPECIAL;

    /*
     * atom        =  1*<any CHAR except specials, SPACE and CTLs>
     *
     * qtext       =  <any CHAR excepting <">,     ; => may be folded
     *                "\" & CR, and including
     *                linear-white-space>
     * dtext       =  <any CHAR excluding "[",     ; => may be folded
     *                "]", "\" & CR, & including
     *                linear-white-space>
     * ctext       =  <any CHAR excluding "(",     ; => may be folded
     *              ")", "\" & CR, & including
     *              linear-white-space>
     */
    for(unsigned i = 0; i < 0xff; i++) {
        if(classes[i] & TOKEN_CHAR) {
            // atom
            if(i == ' ' || (classes[i] & (TOKEN_SPECIAL | TOKEN_CTL))) {
                // no es un atom
            } else {
                classes[i] |= TOKEN_ATOM;
            }

            // qtext
            switch(i) {
                case '"':
                case '\\':
                case '\r':
                    break;
                default:
                    classes[i] |= TOKEN_QTEXT;
            }

            // dtext
            switch(i) {
                case '[':
                case ']':
                case '\\':
                case '\r':
                    break;
                default:
                    classes[i] |= TOKEN_DTEXT;
            }

            // ctext
            switch(i) {
                case '(':
                case ')':
                case '\\':
                case '\r':
                    break;
                default:
                    classes[i] |= TOKEN_CTEXT;
            }

        }
    }

    /*
     * tspecials :=  "(" / ")" / "<" / ">" / "@" /
     *               "," / ";" / ":" / "\" / <">
     *               "/" / "[" / "]" / "?" / "="
     *               ; Must be in quoted-string,
     *               ; to use within parameter values
     */
    classes['(']  |= TOKEN_TSPECIAL;
    classes[')']  |= TOKEN_TSPECIAL;
    classes['<']  |= TOKEN_TSPECIAL;
    classes['>']  |= TOKEN_TSPECIAL;
    classes['@']  |= TOKEN_TSPECIAL;
    classes[',']  |= TOKEN_TSPECIAL;
    classes[';']  |= TOKEN_TSPECIAL;
    classes[':']  |= TOKEN_TSPECIAL;
    classes['\\'] |= TOKEN_TSPECIAL;
    classes['"']  |= TOKEN_TSPECIAL;
    classes['/']  |= TOKEN_TSPECIAL;
    classes[']']  |= TOKEN_TSPECIAL;
    classes['[']  |= TOKEN_TSPECIAL;
    classes['?']  |= TOKEN_TSPECIAL;
    classes['=']  |= TOKEN_TSPECIAL;

    /*
     * bchars        := bcharsnospace / " "
     * bcharsnospace := DIGIT / ALPHA / "'" / "(" / ")" /
     *                     "+" / "_" / "," / "-" / "." /
     *                     "/" / ":" / "=" / "?"
     */
    classes[' ']  |= TOKEN_BCHARS;
    classes['\''] |= TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS;
    classes['(']  |= TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS;
    classes[')']  |= TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS;
    classes['+']  |= TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS;
    classes['_']  |= TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS;
    classes[',']  |= TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS;
    classes['-']  |= TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS;
    classes['.']  |= TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS;
    classes['/']  |= TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS;
    classes[':']  |= TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS;
    classes['=']  |= TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS;
    classes['?']  |= TOKEN_BCHARS_NOSPACE
                  |  TOKEN_BCHARS;

    /*
     *  restricted-name-first  = ALPHA / DIGIT
     *  restricted-name-chars  = ALPHA / DIGIT / "!" / "#" /
     *                             "$" / "&" / "-" / "^" / "_"
     *  restricted-name-chars  =/ "." ; Characters before first dot always
     *                                ; specify a facet name
     *  restricted-name-chars  =/ "+" ; Characters after last plus always
     *                                ; specify a structured syntax suffix
     */
    classes['!']  |= TOKEN_REST_NAME_CHARS;
    classes['#']  |= TOKEN_REST_NAME_CHARS;
    classes['$']  |= TOKEN_REST_NAME_CHARS;
    classes['&']  |= TOKEN_REST_NAME_CHARS;
    classes['-']  |= TOKEN_REST_NAME_CHARS;
    classes['^']  |= TOKEN_REST_NAME_CHARS;
    classes['_']  |= TOKEN_REST_NAME_CHARS;
    classes['.']  |= TOKEN_REST_NAME_CHARS;
    classes['+']  |= TOKEN_REST_NAME_CHARS;

    return classes;
}
