#ifndef ABNF_CHARS_H
#define ABNF_CHARS_H

/**
 * abnf_chars.c -- Characters set defined in the core rules of ABNF
 * Taken from RFC 5234 - Appendix B.
 */

/**
    ALPHA          =  %x41-5A / %x61-7A   ; A-Z / a-z
    BIT            =  "0" / "1"
    CHAR           =  %x01-7F
                        ; any 7-bit US-ASCII character,
                        ;  excluding NUL
    CR             =  %x0D
                        ; carriage return
    CRLF           =  CR LF
                        ; Internet standard newline
    CTL            =  %x00-1F / %x7F
                        ; controls
    DIGIT          =  %x30-39
                        ; 0-9
    DQUOTE         =  %x22
                        ; " (Double Quote)
    HEXDIG         =  DIGIT / "A" / "B" / "C" / "D" / "E" / "F"
    HTAB           =  %x09
                        ; horizontal tab
    LF             =  %x0A
                        ; linefeed
    LWSP           =  *(WSP / CRLF WSP)
                        ; linear-whitespace
    OCTET          =  %x00-FF
                        ; 8 bits of data
    SP             =  %x20
    VCHAR          =  %x21-7E
                        ; visible (printing) characters
    WSP            =  SP / HTAB
                        ; white space
 */

#define TOKEN_DQUOTE 0x22
#define TOKEN_CR 0x0D
#define TOKEN_HTAB 0x09
#define TOKEN_LF 0x0A
#define TOKEN_SP 0x20

enum abnf_char_class {
    // arrancamos en 10 para que sea compatible con los caracteres.
    TOKEN_ALPHA             = 1 << 10,
    TOKEN_CHAR              = 1 << 11,
    TOKEN_CTL               = 1 << 12,
    TOKEN_DIGIT             = 1 << 13,
    TOKEN_HEXDIG            = 1 << 14,
    TOKEN_LWSP              = 1 << 15,
    TOKEN_OCTET             = 1 << 16,
    TOKEN_VCHAR             = 1 << 17,
    TOKEN_WSP               = 1 << 18,
};

/**
 * retorna la caracterización para cada uno de los bytes (256 elementos)
 */
const unsigned *
 init_char_class(void);

#endif
