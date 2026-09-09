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

namespace {

class FakeUploadHandler : public HttpUploadHandler {
public:
    void onUploadStart(const std::string &, const std::string &) override {}
    void onUploadWrite(const uint8_t *, size_t) override {}
    void onUploadEnd() override {}
    void onUploadAborted() override {}
    std::unique_ptr<HttpResponseProducer> finish() override {
        return std::make_unique<ImmediateResponse>(204, "text/plain", "");
    }
};

}  // namespace

void test_upload_route_found_by_method_and_path() {
    HttpRouter router;
    bool factoryCalled = false;
    router.onUpload(HttpMethod::POST, "/ota", [&](const HttpRequest &) {
        factoryCalled = true;
        return std::make_unique<FakeUploadHandler>();
    });

    const auto *factory = router.findUpload(HttpMethod::POST, "/ota");
    TEST_ASSERT_NOT_NULL(factory);
    auto handler = (*factory)(makeRequest(HttpMethod::POST, "/ota"));
    TEST_ASSERT_TRUE(factoryCalled);
    TEST_ASSERT_NOT_NULL(handler.get());
}

void test_upload_route_not_found_for_other_path() {
    HttpRouter router;
    router.onUpload(HttpMethod::POST, "/ota", [&](const HttpRequest &) {
        return std::make_unique<FakeUploadHandler>();
    });

    TEST_ASSERT_NULL(router.findUpload(HttpMethod::POST, "/other"));
    TEST_ASSERT_NULL(router.findUpload(HttpMethod::GET, "/ota"));
}

void test_upload_routes_do_not_interfere_with_normal_routes() {
    HttpRouter router;
    router.onUpload(HttpMethod::POST, "/ota", [&](const HttpRequest &) {
        return std::make_unique<FakeUploadHandler>();
    });
    router.on(HttpMethod::GET, "/status", [&](const HttpRequest &) {
        return std::make_unique<ImmediateResponse>(200, "text/plain", "ok");
    });

    TEST_ASSERT_NULL(router.route(makeRequest(HttpMethod::POST, "/ota")).get());
    TEST_ASSERT_NOT_NULL(router.route(makeRequest(HttpMethod::GET, "/status")).get());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_exact_match_invokes_handler);
    RUN_TEST(test_method_mismatch_does_not_match);
    RUN_TEST(test_path_mismatch_does_not_match);
    RUN_TEST(test_not_found_fallback_invoked);
    RUN_TEST(test_multiple_routes_first_exact_match_wins);
    RUN_TEST(test_upload_route_found_by_method_and_path);
    RUN_TEST(test_upload_route_not_found_for_other_path);
    RUN_TEST(test_upload_routes_do_not_interfere_with_normal_routes);
    return UNITY_END();
}
