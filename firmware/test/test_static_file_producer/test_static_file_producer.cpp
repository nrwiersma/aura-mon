//
// Unit tests for StaticFileProducer against MockSD (multi-file mode, so
// several public/ files - including gzip variants - can exist at once).
//

#include <unity.h>

#include <string>

#include "../stubs/TestCore.h"
#include "../../src/static_file_producer.h"

namespace {

void setFile(const std::string &path, const std::string &content, bool isDir = false) {
    sd.multiFileMode = true;
    auto *f = new FsFile();
    f->data->assign(content.begin(), content.end());
    f->directory = isDir;
    if (sd.namedFiles.count(path)) {
        delete sd.namedFiles[path];
    }
    sd.namedFiles[path] = f;
    sd.namedFileExists[path] = true;
}

// Runs produce() in a loop (as HttpConnection would via onPollTick/
// onWritable), collecting every streamed byte until Done/Error.
struct Result {
    HttpResponseProducer::Status status;
    std::string body;
};

Result runToCompletion(StaticFileProducer &p, size_t bufSize = 8) {
    Result result{};
    std::vector<uint8_t> buf(bufSize);
    size_t written = 0;
    int steps = 0;
    do {
        result.status = p.produce(buf.data(), buf.size(), written);
        if (written > 0) {
            result.body.append(reinterpret_cast<char *>(buf.data()), written);
        }
        steps++;
    } while ((result.status == HttpResponseProducer::Status::Pending ||
              result.status == HttpResponseProducer::Status::Data) &&
             steps < 10000);
    return result;
}

}  // namespace

void setUp() {
    sd.multiFileMode = true;
    for (auto &kv : sd.namedFiles) {
        delete kv.second;
    }
    sd.namedFiles.clear();
    sd.namedFileExists.clear();
}

void tearDown() {}

// ============================================================================
// Basic serving
// ============================================================================

void test_serves_existing_file_with_content_type() {
    setFile("public/app.js", "console.log(1);");
    StaticFileProducer p(HttpMethod::GET, "/app.js");

    Result r = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(r.status));
    TEST_ASSERT_EQUAL(200, p.statusCode());
    TEST_ASSERT_EQUAL_STRING("application/javascript", p.contentType());
    TEST_ASSERT_EQUAL_STRING("console.log(1);", r.body.c_str());
    TEST_ASSERT_NULL(p.extraHeaders());
}

void test_root_path_maps_to_index_html() {
    setFile("public/index.html", "<html></html>");
    StaticFileProducer p(HttpMethod::GET, "/");

    Result r = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(r.status));
    TEST_ASSERT_EQUAL_STRING("text/html", p.contentType());
    TEST_ASSERT_EQUAL_STRING("<html></html>", r.body.c_str());
}

void test_missing_file_returns_404() {
    StaticFileProducer p(HttpMethod::GET, "/missing.txt");

    Result r = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Error), static_cast<int>(r.status));
    TEST_ASSERT_EQUAL(404, p.statusCode());
}

void test_non_get_method_returns_405() {
    setFile("public/app.js", "console.log(1);");
    StaticFileProducer p(HttpMethod::POST, "/app.js");

    Result r = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Error), static_cast<int>(r.status));
    TEST_ASSERT_EQUAL(405, p.statusCode());
}

void test_directory_returns_403() {
    setFile("public/assets", "", /*isDir=*/true);
    StaticFileProducer p(HttpMethod::GET, "/assets");

    Result r = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Error), static_cast<int>(r.status));
    TEST_ASSERT_EQUAL(403, p.statusCode());
}

// ============================================================================
// Gzip variant lookup
// ============================================================================

void test_prefers_gzip_variant_when_present() {
    setFile("public/app.js", "uncompressed");
    setFile("public/app.js.gz", "compressed-bytes");
    StaticFileProducer p(HttpMethod::GET, "/app.js");

    Result r = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(r.status));
    TEST_ASSERT_EQUAL_STRING("compressed-bytes", r.body.c_str());
    TEST_ASSERT_EQUAL_STRING("application/javascript", p.contentType());
    TEST_ASSERT_NOT_NULL(p.extraHeaders());
    TEST_ASSERT_EQUAL_STRING("Content-Encoding: gzip\r\n", p.extraHeaders());
}

void test_falls_back_to_plain_file_when_no_gzip_variant() {
    setFile("public/app.js", "uncompressed");
    StaticFileProducer p(HttpMethod::GET, "/app.js");

    Result r = runToCompletion(p);
    TEST_ASSERT_EQUAL_STRING("uncompressed", r.body.c_str());
    TEST_ASSERT_NULL(p.extraHeaders());
}

// ============================================================================
// Content-Type by extension
// ============================================================================

void test_content_type_by_extension() {
    setFile("public/a.css", "body{}");
    setFile("public/a.json", "{}");
    setFile("public/a.png", "\x89PNG");
    setFile("public/a.svg", "<svg/>");

    {
        StaticFileProducer p(HttpMethod::GET, "/a.css");
        runToCompletion(p);
        TEST_ASSERT_EQUAL_STRING("text/css", p.contentType());
    }
    {
        StaticFileProducer p(HttpMethod::GET, "/a.json");
        runToCompletion(p);
        TEST_ASSERT_EQUAL_STRING("application/json", p.contentType());
    }
    {
        StaticFileProducer p(HttpMethod::GET, "/a.png");
        runToCompletion(p);
        TEST_ASSERT_EQUAL_STRING("image/png", p.contentType());
    }
    {
        StaticFileProducer p(HttpMethod::GET, "/a.svg");
        runToCompletion(p);
        TEST_ASSERT_EQUAL_STRING("image/svg+xml", p.contentType());
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_serves_existing_file_with_content_type);
    RUN_TEST(test_root_path_maps_to_index_html);
    RUN_TEST(test_missing_file_returns_404);
    RUN_TEST(test_non_get_method_returns_405);
    RUN_TEST(test_directory_returns_403);
    RUN_TEST(test_prefers_gzip_variant_when_present);
    RUN_TEST(test_falls_back_to_plain_file_when_no_gzip_variant);
    RUN_TEST(test_content_type_by_extension);
    return UNITY_END();
}
