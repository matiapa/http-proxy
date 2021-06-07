#include <http_chars.h>

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
                  ;
        }
        // 'a' - 'z'
        if(i >= 97 && i <= 122) {
            class |= TOKEN_ALPHA
                  ;
        }

        // '0' - '9'
        if(i >= 48 && i <= 57) {
            class |= TOKEN_DIGIT

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
    classes['/']  |= TOKEN_SPECIAL;
    classes['\\'] |= TOKEN_SPECIAL;
    classes['"']  |= TOKEN_SPECIAL;
    classes['*']  |= TOKEN_SPECIAL;
    classes[':']  |= TOKEN_SPECIAL;
    classes['.']  |= TOKEN_SPECIAL;
    classes[']']  |= TOKEN_SPECIAL;
    classes['[']  |= TOKEN_SPECIAL;


    return classes;
}
