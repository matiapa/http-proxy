#ifndef CHARS_H_de86f40a0f68879e2eab1005443f429df605a419_
#define CHARS_H_de86f40a0f68879e2eab1005443f429df605a419_

/**
 * mime_chars.c -- caracterización de caracteres RFC822/y lo relacionados a MIME
 *
 * Cada valor de un byte (0-255) es caracterizado quedando dichas
 * caracterizaciones en una tabla para una rápida consulta.
 *
 * Las caracterizaciones vienen de las gramáticas de varios RFCs.
 * Por ejemplo del RFC822 se toma CHAR, ALPHA, DIGIT, ….
 *
 */

/**
 * RFC822:
 *
 * CHAR        =  <any ASCII character>        ; (  0-177,  0.-127.)
 *                                             ; (  Octal, Decimal.)
 * ALPHA       =  <any ASCII alphabetic character>
 *                                             ; (101-132, 65.- 90.)
 *                                             ; (141-172, 97.-122.)
 * DIGIT       =  <any ASCII decimal digit>    ; ( 60- 71, 48.- 57.)
 * CTL         =  <any ASCII control           ; (  0- 37,  0.- 31.)
 *                 character and DEL>          ; (    177,     127.)
 * CR          =  <ASCII CR, carriage return>  ; (     15,      13.)
 * LF          =  <ASCII LF, linefeed>         ; (     12,      10.)
 * SPACE       =  <ASCII SP, space>            ; (     40,      32.)
 * HTAB        =  <ASCII HT, horizontal-tab>   ; (     11,       9.)
 * <">         =  <ASCII quote mark>           ; (     42,      34.)
 * CRLF        =  CR LF
 *
 * LWSP-char   =  SPACE / HTAB                 ; semantics = SPACE
 *
 * specials    =  "(" / ")" / "<" / ">" / "@"    ; Must be in quoted-
 *               /  "," / ";" / ":" / "\" / <">  ;  string, to use
 *               /  "." / "[" / "]"              ;  within a word.
 * atom        =  1*<any CHAR except specials, SPACE and CTLs>
 *
 * qtext       =  <any CHAR excepting <">,     ; => may be folded
 *                "\" & CR, and including
 *                linear-white-space>
 * dtext       =  <any CHAR excluding "[",     ; => may be folded
 *                "]", "\" & CR, & including
 *                linear-white-space>
 * ctext       =  <any CHAR excluding "(",     ; => may be folded
 *                ")", "\" & CR, & including
 *                linear-white-space>
 *
 * RFC2045 (MIME Part One: Format of Internet Message Bodies)
 *
 * token := 1*<any (US-ASCII) CHAR except SPACE, CTLs,
 *                or tspecials>
 *
 * tspecials :=  "(" / ")" / "<" / ">" / "@" /
 *               "," / ";" / ":" / "\" / <">
 *               "/" / "[" / "]" / "?" / "="
 *               ; Must be in quoted-string,
 *
 *               ; to use within parameter values
 * RFC2046 (MIME Part Two: Media Types)
 *
 * bchars        := bcharsnospace / " "
 * bcharsnospace := DIGIT / ALPHA / "'" / "(" / ")" /
 *                     "+" / "_" / "," / "-" / "." /
 *                     "/" / ":" / "=" / "?"
 * RFC6838 - Media Type Specifications and Registration Procedures
 *  restricted-name = restricted-name-first *126restricted-name-chars
 *  restricted-name-first  = ALPHA / DIGIT
 *  restricted-name-chars  = ALPHA / DIGIT / "!" / "#" /
 *                             "$" / "&" / "-" / "^" / "_"
 *  restricted-name-chars  =/ "." ; Characters before first dot always
 *                                ; specify a facet name
 *  restricted-name-chars  =/ "+" ; Characters after last plus always
 *                                ; specify a structured syntax suffix
 */
enum mime_char_class {
    // arrancamos en 10 para que sea compatible con los caracteres.
    TOKEN_CHAR              = 1 << 10,
    TOKEN_ALPHA             = 1 << 11,
    TOKEN_DIGIT             = 1 << 12,
    TOKEN_CTL               = 1 << 13,
    TOKEN_LWSP              = 1 << 14,
    TOKEN_SPECIAL           = 1 << 15,
    TOKEN_ATOM              = 1 << 16,
    TOKEN_QTEXT             = 1 << 18,
    TOKEN_DTEXT             = 1 << 19,
    TOKEN_CTEXT             = 1 << 20,
    TOKEN_BCHARS            = 1 << 21,
    TOKEN_BCHARS_NOSPACE    = 1 << 22,
    TOKEN_REST_NAME_FIRST   = 1 << 23,
    TOKEN_REST_NAME_CHARS   = 1 << 24,
    TOKEN_TSPECIAL          = 1 << 25,
};

/**
 * retorna la caracterización para cada uno de los bytes (256 elementos)
 */
const unsigned *
init_char_class(void);

#endif
