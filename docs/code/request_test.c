#include <stdlib.h>
#include <string.h>
#include <check.h>

#include "request.h"
#include "tests.h"

#define FIXBUF(b, data) buffer_init(&(b), N(data), (data)); \
                        buffer_write_adv(&(b), N(data))

START_TEST (test_request_unsuppored_version) {
    struct request request;
    struct request_parser parser = {
        .request = &request,
    };
    request_parser_init(&parser);
    uint8_t data[] = {
        0x04,
    };
    buffer b; FIXBUF(b, data);
    bool errored = false;
    enum request_state st = request_consume(&b, &parser, &errored);
    
    ck_assert_uint_eq(true, errored);
    ck_assert_uint_eq(request_error_unsupported_version,     st);

}
END_TEST


START_TEST (test_request_connect_domain) {
    struct request request;
    struct request_parser parser = {
        .request = &request,
    };
    request_parser_init(&parser);
    uint8_t data[] = {
        0x05, 0x01, 0x00, 0x03, 0x0f, 0x77, 0x77, 0x77, 
        0x2e, 0x69, 0x74, 0x62, 0x61, 0x2e, 0x65, 0x64, 
        0x75, 0x2e, 0x61, 0x72, 0x00, 0x50, 
    };
    buffer b; FIXBUF(b, data);
    bool errored = false;
    request_consume(&b, &parser, &errored);
    
    ck_assert_uint_eq(false, errored);
    ck_assert_uint_eq(socks_req_cmd_connect,     request.cmd);
    ck_assert_uint_eq(socks_req_addrtype_domain, request.dest_addr_type);
    ck_assert_str_eq ("www.itba.edu.ar",         request.dest_addr.fqdn);
    ck_assert_uint_eq(htons(80),                 request.dest_port);

}
END_TEST


START_TEST (test_request_connect_ipv4) {
    struct request request;
    struct request_parser parser = {
        .request = &request,
    };
    request_parser_init(&parser);

    uint8_t data[] = {
        0x05, 0x01, 0x00, 0x01, 0x7f, 0x00, 0x00, 0x01,
        0x23, 0x82
    };
    buffer b; FIXBUF(b, data);
    bool errored = false;
    enum request_state st = request_consume(&b, &parser, &errored);
    
    ck_assert_uint_eq(false, errored);
    ck_assert_uint_eq(request_done,              st);
    ck_assert_uint_eq(socks_req_cmd_connect,     request.cmd);
    ck_assert_uint_eq(socks_req_addrtype_ipv4,   request.dest_addr_type);
    ck_assert_uint_eq(htons(9090),               request.dest_port);

}
END_TEST

START_TEST (test_request_connect_ipv6) {
    struct request request;
    struct request_parser parser = {
        .request = &request,
    };
    request_parser_init(&parser);

    uint8_t data[] = {
        0x05, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x23, 0x82 
    };
    buffer b; FIXBUF(b, data);
    bool errored = false;
    enum request_state st = request_consume(&b, &parser, &errored);
    
    ck_assert_uint_eq(false, errored);
    ck_assert_uint_eq(request_done,              st);
    ck_assert_uint_eq(socks_req_cmd_connect,     request.cmd);
    ck_assert_uint_eq(socks_req_addrtype_ipv6,   request.dest_addr_type);
    ck_assert_uint_eq(htons(9090),               request.dest_port);

}
END_TEST

START_TEST (test_request_connect_multiple_messages) {
    struct request request;
    struct request_parser parser = {
        .request = &request,
    };
    request_parser_init(&parser);
    uint8_t data[] = {
        // 00
        0x05, 0x01, 0x00, 0x03, 0x0f, 0x77, 0x77, 0x77,
        0x2e, 0x69, 0x74, 0x62, 0x61, 0x2e, 0x65, 0x64,
        0x75, 0x2e, 0x61, 0x72, 0x00, 0x50,
        // 01
        0x05, 0x01, 0x00, 0x03, 0x0f, 0x77, 0x77, 0x77,
        0x2e, 0x69, 0x74, 0x62, 0x61, 0x2e, 0x65, 0x64,
        0x75, 0x2e, 0x61, 0x72, 0x00, 0x50,
    };
    buffer b; FIXBUF(b, data);
    bool errored = false;
    request_consume(&b, &parser, &errored);

    ck_assert_uint_eq(false, errored);
    ck_assert_uint_eq(socks_req_cmd_connect,     request.cmd);
    ck_assert_uint_eq(socks_req_addrtype_domain, request.dest_addr_type);
    ck_assert_str_eq ("www.itba.edu.ar",         request.dest_addr.fqdn);
    ck_assert_uint_eq(htons(80),                 request.dest_port);

    errored = false;
    memset(&request, 0, sizeof(request));
    request_parser_init(&parser);

    request_consume(&b, &parser, &errored);
    ck_assert_uint_eq(false, errored);
    ck_assert_uint_eq(socks_req_cmd_connect,     request.cmd);
    ck_assert_uint_eq(socks_req_addrtype_domain, request.dest_addr_type);
    ck_assert_str_eq ("www.itba.edu.ar",         request.dest_addr.fqdn);
    ck_assert_uint_eq(htons(80),                 request.dest_port);
}
END_TEST

Suite * 
request_suite(void) {
    Suite *s;
    TCase *tc;

    s = suite_create("socks");

    /* Core test case */
    tc = tcase_create("request");

    tcase_add_test(tc, test_request_unsuppored_version);
    tcase_add_test(tc, test_request_connect_domain);
    tcase_add_test(tc, test_request_connect_ipv4);
    tcase_add_test(tc, test_request_connect_ipv6);
    tcase_add_test(tc, test_request_connect_multiple_messages);

    suite_add_tcase(s, tc);

    return s;
}

int 
main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = request_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

