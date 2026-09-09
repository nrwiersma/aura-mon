//
// Unit tests for HttpRequestParser: split/partial reads, query strings,
// headers, Content-Length bodies, and malformed input.
//

#include <unity.h>

#include <cstring>

#include "../../lib/AsyncHttpServer/src/HttpRequestParser.h"

static HttpParseStatus feed(HttpRequestParser &p, const std::string &s) {
    return p.feed(reinterpret_cast<const uint8_t *>(s.data()), s.size());
}

void setUp() {}
void tearDown() {}

// ============================================================================
// Basic requests
// ============================================================================

void test_simple_get_no_headers_body() {
    HttpRequestParser p;
    HttpParseStatus status = feed(p, "GET /status HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(static_cast<int>(HttpParseStatus::Complete), static_cast<int>(status));
    TEST_ASSERT_EQUAL(static_cast<int>(HttpMethod::GET), static_cast<int>(p.request().method));
    TEST_ASSERT_EQUAL_STRING("/status", p.request().path.c_str());
    TEST_ASSERT_TRUE(p.request().body.empty());
}

void test_headers_are_lowercased_and_trimmed() {
    HttpRequestParser p;
    feed(p, "GET / HTTP/1.1\r\nHost: example.com\r\nX-Test:  value \r\n\r\n");
    const std::string *host = p.request().header("host");
    TEST_ASSERT_NOT_NULL(host);
    TEST_ASSERT_EQUAL_STRING("example.com", host->c_str());
    const std::string *xtest = p.request().header("x-test");
    TEST_ASSERT_NOT_NULL(xtest);
    TEST_ASSERT_EQUAL_STRING("value", xtest->c_str());
}

// ============================================================================
// Query strings
// ============================================================================

void test_query_string_parsed() {
    HttpRequestParser p;
    feed(p, "GET /logs?start=10&limit=20 HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_STRING("/logs", p.request().path.c_str());
    const std::string *start = p.request().queryParam("start");
    const std::string *limit = p.request().queryParam("limit");
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_NOT_NULL(limit);
    TEST_ASSERT_EQUAL_STRING("10", start->c_str());
    TEST_ASSERT_EQUAL_STRING("20", limit->c_str());
}

void test_query_string_url_decoded() {
    HttpRequestParser p;
    feed(p, "GET /search?q=hello%20world&x=a%2Bb HTTP/1.1\r\n\r\n");
    const std::string *q = p.request().queryParam("q");
    const std::string *x = p.request().queryParam("x");
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_NOT_NULL(x);
    TEST_ASSERT_EQUAL_STRING("hello world", q->c_str());
    TEST_ASSERT_EQUAL_STRING("a+b", x->c_str());
}

// ============================================================================
// Body / Content-Length
// ============================================================================

void test_body_with_content_length() {
    HttpRequestParser p;
    HttpParseStatus status = feed(p, "POST /config HTTP/1.1\r\nContent-Length: 13\r\n\r\n{\"a\":\"b\"}xyz");
    TEST_ASSERT_EQUAL(static_cast<int>(HttpParseStatus::NeedMoreData), static_cast<int>(status));
    status = feed(p, "z");
    TEST_ASSERT_EQUAL(static_cast<int>(HttpParseStatus::Complete), static_cast<int>(status));
    TEST_ASSERT_EQUAL_STRING("{\"a\":\"b\"}xyzz", p.request().body.c_str());
}

void test_missing_content_length_gives_empty_body() {
    HttpRequestParser p;
    HttpParseStatus status = feed(p, "GET /status HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_EQUAL(static_cast<int>(HttpParseStatus::Complete), static_cast<int>(status));
    TEST_ASSERT_TRUE(p.request().body.empty());
}

// ============================================================================
// Byte-at-a-time / arbitrary split feeding
// ============================================================================

void test_request_split_across_many_small_feeds() {
    HttpRequestParser p;
    std::string full = "POST /config HTTP/1.1\r\nContent-Length: 5\r\nHost: h\r\n\r\nhello";
    HttpParseStatus status = HttpParseStatus::NeedMoreData;
    for (size_t i = 0; i < full.size(); i++) {
        status = feed(p, full.substr(i, 1));
    }
    TEST_ASSERT_EQUAL(static_cast<int>(HttpParseStatus::Complete), static_cast<int>(status));
    TEST_ASSERT_EQUAL_STRING("hello", p.request().body.c_str());
    TEST_ASSERT_EQUAL_STRING("/config", p.request().path.c_str());
}

void test_header_line_split_across_reads() {
    HttpRequestParser p;
    feed(p, "GET / HTTP/1.1\r\nHo");
    HttpParseStatus status = feed(p, "st: example.com\r\n\r\n");
    TEST_ASSERT_EQUAL(static_cast<int>(HttpParseStatus::Complete), static_cast<int>(status));
    const std::string *host = p.request().header("host");
    TEST_ASSERT_NOT_NULL(host);
    TEST_ASSERT_EQUAL_STRING("example.com", host->c_str());
}

// ============================================================================
// Errors
// ============================================================================

void test_malformed_request_line_is_error() {
    HttpRequestParser p;
    HttpParseStatus status = feed(p, "NOTVALIDLINE\r\n\r\n");
    TEST_ASSERT_EQUAL(static_cast<int>(HttpParseStatus::Error), static_cast<int>(status));
}

void test_unknown_method_is_error() {
    HttpRequestParser p;
    HttpParseStatus status = feed(p, "FROB / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(static_cast<int>(HttpParseStatus::Error), static_cast<int>(status));
}

void test_malformed_content_length_is_error() {
    HttpRequestParser p;
    HttpParseStatus status = feed(p, "POST /x HTTP/1.1\r\nContent-Length: notanumber\r\n\r\n");
    TEST_ASSERT_EQUAL(static_cast<int>(HttpParseStatus::Error), static_cast<int>(status));
}

void test_oversized_body_is_error() {
    HttpRequestParser p;
    HttpParseStatus status = feed(p, "POST /x HTTP/1.1\r\nContent-Length: 999999999\r\n\r\n");
    TEST_ASSERT_EQUAL(static_cast<int>(HttpParseStatus::Error), static_cast<int>(status));
}

void test_reset_allows_reuse() {
    HttpRequestParser p;
    feed(p, "GET /a HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(static_cast<int>(HttpMethod::GET), static_cast<int>(p.request().method));
    p.reset();
    HttpParseStatus status = feed(p, "POST /b HTTP/1.1\r\nContent-Length: 1\r\n\r\nx");
    TEST_ASSERT_EQUAL(static_cast<int>(HttpParseStatus::Complete), static_cast<int>(status));
    TEST_ASSERT_EQUAL_STRING("/b", p.request().path.c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_simple_get_no_headers_body);
    RUN_TEST(test_headers_are_lowercased_and_trimmed);
    RUN_TEST(test_query_string_parsed);
    RUN_TEST(test_query_string_url_decoded);
    RUN_TEST(test_body_with_content_length);
    RUN_TEST(test_missing_content_length_gives_empty_body);
    RUN_TEST(test_request_split_across_many_small_feeds);
    RUN_TEST(test_header_line_split_across_reads);
    RUN_TEST(test_malformed_request_line_is_error);
    RUN_TEST(test_unknown_method_is_error);
    RUN_TEST(test_malformed_content_length_is_error);
    RUN_TEST(test_oversized_body_is_error);
    RUN_TEST(test_reset_allows_reuse);
    return UNITY_END();
}
