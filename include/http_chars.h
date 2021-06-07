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

 */
enum mime_char_class {
    // arrancamos en 10 para que sea compatible con los caracteres.
    TOKEN_CHAR              = 1 << 10,
    TOKEN_ALPHA             = 1 << 11,
    TOKEN_DIGIT             = 1 << 12,
    TOKEN_CTL               = 1 << 13,
    TOKEN_LWSP              = 1 << 14,
    TOKEN_SPECIAL           = 1 << 15,

};

/**
 * retorna la caracterización para cada uno de los bytes (256 elementos)
 */
const unsigned *
 init_char_class(void);

#endif
