#include <stdio.h>
#include <stdlib.h>
#include <check.h>

#include "parser.h"
#include "mime_chars.h"
#include "mime_msg.h"

static void
assert_eq_empty(const unsigned type, const struct parser_event *e) {
    ck_assert_uint_eq(type, e->type);
    ck_assert_ptr_eq (0,    e->next);
    ck_assert_uint_eq(0,    e->n);
}

static void
assert_eq_one(const unsigned type, const int c, const struct parser_event *e) {
    ck_assert_uint_eq(type, e->type);
    ck_assert_uint_eq(1,    e->n);
    ck_assert_uint_eq(c,    e->data[0]);
}

static void
assert_eq_two(const unsigned type, const int c, const int d,
              const struct parser_event *e) {
    ck_assert_uint_eq(type, e->type);
    ck_assert_uint_eq(2,    e->n);
    ck_assert_uint_eq(c,    e->data[0]);
    ck_assert_uint_eq(d,    e->data[1]);
}


START_TEST (test_mime_normal) {
    struct parser *p = parser_init(init_char_class(), mime_message_parser());
    const struct parser_event* e;

    e = parser_feed  (p, 'f');
    assert_eq_one    (MIME_MSG_NAME,     'f', e);
    ck_assert_ptr_eq (0,   e->next);

    e = parser_feed  (p, ':');
    assert_eq_one    (MIME_MSG_NAME_END, ':', e);
    ck_assert_ptr_eq (0,   e->next);

    e = parser_feed  (p, 'b');
    assert_eq_one    (MIME_MSG_VALUE,    'b', e);
    ck_assert_ptr_eq (0,   e->next);

    e = parser_feed  (p, '\r');
    assert_eq_empty  (MIME_MSG_WAIT,          e);
    ck_assert_ptr_eq (0,   e->next);

    e = parser_feed  (p, '\n');
    assert_eq_empty  (MIME_MSG_WAIT,          e);
    ck_assert_ptr_eq (0,   e->next);

    e = parser_feed  (p, ' ');
    assert_eq_two    (MIME_MSG_VALUE_FOLD, '\r', '\n', e);
    assert_eq_one    (MIME_MSG_VALUE_FOLD, ' ',        e->next);
    ck_assert_ptr_eq (0,   e->next->next);

    e = parser_feed  (p, '\t');
    assert_eq_one    (MIME_MSG_VALUE_FOLD, '\t', e);
    ck_assert_ptr_eq (0,   e->next);

    e = parser_feed  (p, 'x');
    assert_eq_one    (MIME_MSG_VALUE,      'x',  e);
    ck_assert_ptr_eq (0,   e->next);

    e = parser_feed  (p, '\r');
    assert_eq_empty  (MIME_MSG_WAIT,             e);
    ck_assert_ptr_eq (0,   e->next);

    e = parser_feed  (p, 'y');
    assert_eq_one    (MIME_MSG_VALUE,     '\r', e);
    assert_eq_one    (MIME_MSG_VALUE,     'y',  e->next);
    ck_assert_ptr_eq (0,   e->next->next);

    e = parser_feed  (p, '\r');
    assert_eq_empty  (MIME_MSG_WAIT,          e);
    ck_assert_ptr_eq (0,   e->next);

    e = parser_feed  (p, '\n');
    assert_eq_empty  (MIME_MSG_WAIT,          e);
    ck_assert_ptr_eq (0,   e->next);

    e = parser_feed  (p, '\r');
    assert_eq_empty  (MIME_MSG_WAIT,          e);
    ck_assert_ptr_eq (0,   e->next);

    e = parser_feed  (p, '\n');
    assert_eq_two    (MIME_MSG_VALUE_END,  '\r', '\n', e);
    assert_eq_two    (MIME_MSG_BODY_START, '\r', '\n', e->next);
    ck_assert_ptr_eq (0,   e->next->next);

    e = parser_feed  (p, 'B');
    assert_eq_one    (MIME_MSG_BODY,    'B', e);
    ck_assert_ptr_eq (0,   e->next);

    e = parser_feed  (p, 'o');
    assert_eq_one    (MIME_MSG_BODY,    'o', e);
    ck_assert_ptr_eq (0,   e->next);

    parser_destroy(p);
}

END_TEST

Suite *
suite(void) {
    Suite *s;
    TCase *tc;

    s = suite_create("mime");

    /* Core test case */
    tc = tcase_create("mime");

    tcase_add_test(tc, test_mime_normal);
    suite_add_tcase(s, tc);

    return s;
}

int
main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

