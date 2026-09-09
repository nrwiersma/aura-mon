//
// Unit tests for HttpRouter: method/path matching and not-found fallback.
//

#include <unity.h>

#include "../../lib/AsyncHttpServer/src/HttpRouter.h"
#include "../../lib/AsyncHttpServer/src/ImmediateResponse.h"

void setUp() {}
void tearDown() {}

static HttpRequest makeRequest(HttpMethod m, const std::string &path) {
    HttpRequest r;
    r.method = m;
    r.path = path;
    return r;
}

void test_exact_match_invokes_handler() {
    HttpRouter router;
    bool called = false;
    router.on(HttpMethod::GET, "/status", [&](const HttpRequest &) {
        called = true;
        return std::make_unique<ImmediateResponse>(200, "text/plain", "ok");
    });

    auto producer = router.route(makeRequest(HttpMethod::GET, "/status"));
    TEST_ASSERT_TRUE(called);
    TEST_ASSERT_NOT_NULL(producer.get());
}

void test_method_mismatch_does_not_match() {
    HttpRouter router;
    router.on(HttpMethod::GET, "/status", [&](const HttpRequest &) {
        return std::make_unique<ImmediateResponse>(200, "text/plain", "ok");
    });

    auto producer = router.route(makeRequest(HttpMethod::POST, "/status"));
    TEST_ASSERT_NULL(producer.get());
}

void test_path_mismatch_does_not_match() {
    HttpRouter router;
    router.on(HttpMethod::GET, "/status", [&](const HttpRequest &) {
        return std::make_unique<ImmediateResponse>(200, "text/plain", "ok");
    });

    auto producer = router.route(makeRequest(HttpMethod::GET, "/other"));
    TEST_ASSERT_NULL(producer.get());
}

void test_not_found_fallback_invoked() {
    HttpRouter router;
    bool notFoundCalled = false;
    router.onNotFound([&](const HttpRequest &) {
        notFoundCalled = true;
        return std::make_unique<ImmediateResponse>(404, "application/json", "{}");
    });

    auto producer = router.route(makeRequest(HttpMethod::GET, "/nope"));
    TEST_ASSERT_TRUE(notFoundCalled);
    TEST_ASSERT_NOT_NULL(producer.get());
}

void test_multiple_routes_first_exact_match_wins() {
    HttpRouter router;
    router.on(HttpMethod::GET, "/a", [&](const HttpRequest &) {
        return std::make_unique<ImmediateResponse>(200, "text/plain", "a");
    });
    router.on(HttpMethod::GET, "/b", [&](const HttpRequest &) {
        return std::make_unique<ImmediateResponse>(200, "text/plain", "b");
    });

    size_t written = 0;
    uint8_t buf[16];
    auto producer = router.route(makeRequest(HttpMethod::GET, "/b"));
    TEST_ASSERT_NOT_NULL(producer.get());
    producer->produce(buf, sizeof(buf), written);
    TEST_ASSERT_EQUAL(1, written);
    TEST_ASSERT_EQUAL('b', buf[0]);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_exact_match_invokes_handler);
    RUN_TEST(test_method_mismatch_does_not_match);
    RUN_TEST(test_path_mismatch_does_not_match);
    RUN_TEST(test_not_found_fallback_invoked);
    RUN_TEST(test_multiple_routes_first_exact_match_wins);
    return UNITY_END();
}
