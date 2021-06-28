#include <abnf_chars.h>

static unsigned classes[256];

/** inicializa la caracterización de cada byte */
const unsigned *
init_char_class(void) {
    for(unsigned i = 0; i < 0xff; i++) {
        unsigned class = 0x00;

        class |= TOKEN_OCTET;

        if(i >= 0x01 && i <= 0x7F) {
            class |= TOKEN_CHAR;
        }

        if(i >= 0x21 && i <= 0x7E) {
            class |= TOKEN_VCHAR;
        }

        // 'A' - 'Z'
        if(i >= 0x41 && i <= 0x5A) {
            class |= TOKEN_ALPHA;
        }

        // 'a' - 'z'
        if(i >= 0x61 && i <= 0x7A) {
            class |= TOKEN_ALPHA;
        }

        // 'A' - 'F'
        if(i >= 0x41 && i <= 0x46) {
            class |= TOKEN_HEXDIG;
        }

        // '0' - '9'
        if(i >= 0x30 && i <= 0x39) {
            class |= TOKEN_DIGIT;
            class |= TOKEN_HEXDIG;
        }

        if(i <= 0x1F || i == 0x7F) {
            class |= TOKEN_CTL;
        }

        classes[i] = class;
    }

    classes[TOKEN_SP]    |= TOKEN_WSP;
    classes[TOKEN_HTAB]  |= TOKEN_WSP;

    return classes;
}
