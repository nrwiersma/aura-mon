//
// Unit tests for HttpRouter: method/path matching, not-found fallback,
// upload-route lookup.
//

#include <unity.h>

#include "../../lib/AsyncHttpServer/src/HttpRouter.h"

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
    router.on(HttpMethod::GET, "/status", [&](HttpRequest &) { called = true; });

    auto req = makeRequest(HttpMethod::GET, "/status");
    router.route(req);
    TEST_ASSERT_TRUE(called);
}

void test_method_mismatch_does_not_match() {
    HttpRouter router;
    bool called = false;
    router.on(HttpMethod::GET, "/status", [&](HttpRequest &) { called = true; });

    auto req = makeRequest(HttpMethod::POST, "/status");
    router.route(req);
    TEST_ASSERT_FALSE(called);
}

void test_path_mismatch_does_not_match() {
    HttpRouter router;
    bool called = false;
    router.on(HttpMethod::GET, "/status", [&](HttpRequest &) { called = true; });

    auto req = makeRequest(HttpMethod::GET, "/other");
    router.route(req);
    TEST_ASSERT_FALSE(called);
}

void test_not_found_fallback_invoked() {
    HttpRouter router;
    bool notFoundCalled = false;
    router.onNotFound([&](HttpRequest &) { notFoundCalled = true; });

    auto req = makeRequest(HttpMethod::GET, "/nope");
    router.route(req);
    TEST_ASSERT_TRUE(notFoundCalled);
}

void test_multiple_routes_first_exact_match_wins() {
    HttpRouter router;
    std::string hit;
    router.on(HttpMethod::GET, "/a", [&](HttpRequest &) { hit = "a"; });
    router.on(HttpMethod::GET, "/b", [&](HttpRequest &) { hit = "b"; });

    auto req = makeRequest(HttpMethod::GET, "/b");
    router.route(req);
    TEST_ASSERT_EQUAL_STRING("b", hit.c_str());
}

namespace {

class FakeUploadHandler : public HttpUploadHandler {
public:
    void onUploadStart(const std::string &, const std::string &) override {}
    void onUploadWrite(const uint8_t *, size_t) override {}
    void onUploadEnd() override {}
    void onUploadAborted() override {}
    void finish(HttpRequest &) override {}
};

}  // namespace

void test_upload_route_found_by_method_and_path() {
    HttpRouter router;
    bool factoryCalled = false;
    router.onUpload(HttpMethod::POST, "/ota", [&](HttpRequest &) {
        factoryCalled = true;
        return std::make_unique<FakeUploadHandler>();
    });

    const auto *factory = router.findUpload(HttpMethod::POST, "/ota");
    TEST_ASSERT_NOT_NULL(factory);
    auto req = makeRequest(HttpMethod::POST, "/ota");
    auto handler = (*factory)(req);
    TEST_ASSERT_TRUE(factoryCalled);
    TEST_ASSERT_NOT_NULL(handler.get());
}

void test_upload_route_not_found_for_other_path() {
    HttpRouter router;
    router.onUpload(HttpMethod::POST, "/ota", [&](HttpRequest &) {
        return std::make_unique<FakeUploadHandler>();
    });

    TEST_ASSERT_NULL(router.findUpload(HttpMethod::POST, "/other"));
    TEST_ASSERT_NULL(router.findUpload(HttpMethod::GET, "/ota"));
}

void test_upload_routes_do_not_interfere_with_normal_routes() {
    HttpRouter router;
    router.onUpload(HttpMethod::POST, "/ota", [&](HttpRequest &) {
        return std::make_unique<FakeUploadHandler>();
    });
    bool statusCalled = false;
    router.on(HttpMethod::GET, "/status", [&](HttpRequest &) { statusCalled = true; });

    auto uploadReq = makeRequest(HttpMethod::POST, "/ota");
    router.route(uploadReq);  // no normal route matches: default 404 send (no-op, no conn)
    TEST_ASSERT_FALSE(statusCalled);

    auto statusReq = makeRequest(HttpMethod::GET, "/status");
    router.route(statusReq);
    TEST_ASSERT_TRUE(statusCalled);
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
