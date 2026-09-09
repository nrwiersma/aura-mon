//
// Unit tests for MultipartParser: single/multi-part bodies, boundary splits
// across feed() calls, and bounded-memory large-file streaming.
//

#include <unity.h>

#include <vector>

#include "../../lib/AsyncHttpServer/src/MultipartParser.h"

namespace {

struct RecordedPart {
    std::string name;
    std::string filename;
    std::string data;
    bool ended = false;
};

class RecordingDelegate : public MultipartParser::Delegate {
public:
    void onPartBegin(const std::string &name, const std::string &filename) override {
        parts.emplace_back();
        parts.back().name = name;
        parts.back().filename = filename;
    }
    void onPartData(const uint8_t *data, size_t len) override {
        TEST_ASSERT_FALSE(parts.empty());
        parts.back().data.append(reinterpret_cast<const char *>(data), len);
    }
    void onPartEnd() override {
        TEST_ASSERT_FALSE(parts.empty());
        parts.back().ended = true;
    }

    std::vector<RecordedPart> parts;
};

}  // namespace

void setUp() {}
void tearDown() {}

void test_single_part_upload() {
    RecordingDelegate d;
    MultipartParser p("XBOUNDARY", d);
    std::string body =
        "--XBOUNDARY\r\n"
        "Content-Disposition: form-data; name=\"firmware\"; filename=\"fw.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "BINARYDATA"
        "\r\n--XBOUNDARY--\r\n";
    p.feed(reinterpret_cast<const uint8_t *>(body.data()), body.size());

    TEST_ASSERT_TRUE(p.isDone());
    TEST_ASSERT_FALSE(p.hasError());
    TEST_ASSERT_EQUAL(1, static_cast<int>(d.parts.size()));
    TEST_ASSERT_EQUAL_STRING("firmware", d.parts[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("fw.bin", d.parts[0].filename.c_str());
    TEST_ASSERT_EQUAL_STRING("BINARYDATA", d.parts[0].data.c_str());
    TEST_ASSERT_TRUE(d.parts[0].ended);
}

void test_multi_part_mixed_fields() {
    RecordingDelegate d;
    MultipartParser p("B", d);
    std::string body =
        "--B\r\n"
        "Content-Disposition: form-data; name=\"note\"\r\n"
        "\r\n"
        "hello\r\n"
        "--B\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"a.txt\"\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "file-contents-here\r\n"
        "--B--\r\n";
    p.feed(reinterpret_cast<const uint8_t *>(body.data()), body.size());

    TEST_ASSERT_TRUE(p.isDone());
    TEST_ASSERT_EQUAL(2, static_cast<int>(d.parts.size()));
    TEST_ASSERT_EQUAL_STRING("note", d.parts[0].name.c_str());
    TEST_ASSERT_TRUE(d.parts[0].filename.empty());
    TEST_ASSERT_EQUAL_STRING("hello", d.parts[0].data.c_str());
    TEST_ASSERT_EQUAL_STRING("file", d.parts[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("a.txt", d.parts[1].filename.c_str());
    TEST_ASSERT_EQUAL_STRING("file-contents-here", d.parts[1].data.c_str());
}

void test_boundary_split_across_many_small_feeds() {
    RecordingDelegate d;
    MultipartParser p("XBOUNDARY", d);
    std::string body =
        "--XBOUNDARY\r\n"
        "Content-Disposition: form-data; name=\"f\"; filename=\"f.bin\"\r\n"
        "\r\n"
        "0123456789"
        "\r\n--XBOUNDARY--\r\n";
    for (size_t i = 0; i < body.size(); i++) {
        p.feed(reinterpret_cast<const uint8_t *>(body.data() + i), 1);
    }

    TEST_ASSERT_TRUE(p.isDone());
    TEST_ASSERT_EQUAL(1, static_cast<int>(d.parts.size()));
    TEST_ASSERT_EQUAL_STRING("0123456789", d.parts[0].data.c_str());
}

void test_large_file_delivered_in_chunks_stays_bounded() {
    RecordingDelegate d;
    MultipartParser p("B", d);
    std::string preamble =
        "--B\r\n"
        "Content-Disposition: form-data; name=\"f\"; filename=\"big.bin\"\r\n"
        "\r\n";
    p.feed(reinterpret_cast<const uint8_t *>(preamble.data()), preamble.size());

    // Feed 64KB of body data, well beyond any header/body cap used
    // elsewhere in the parser, in small chunks.
    std::string chunk(4096, 'z');
    size_t total = 0;
    for (int i = 0; i < 16; i++) {
        p.feed(reinterpret_cast<const uint8_t *>(chunk.data()), chunk.size());
        total += chunk.size();
    }
    std::string trailer = "\r\n--B--\r\n";
    p.feed(reinterpret_cast<const uint8_t *>(trailer.data()), trailer.size());

    TEST_ASSERT_TRUE(p.isDone());
    TEST_ASSERT_EQUAL(1, static_cast<int>(d.parts.size()));
    TEST_ASSERT_EQUAL(total, d.parts[0].data.size());
}

void test_preamble_before_first_boundary_is_ignored() {
    RecordingDelegate d;
    MultipartParser p("B", d);
    std::string body =
        "ignored preamble text\r\n"
        "--B\r\n"
        "Content-Disposition: form-data; name=\"x\"\r\n"
        "\r\n"
        "value"
        "\r\n--B--\r\n";
    p.feed(reinterpret_cast<const uint8_t *>(body.data()), body.size());

    TEST_ASSERT_TRUE(p.isDone());
    TEST_ASSERT_EQUAL(1, static_cast<int>(d.parts.size()));
    TEST_ASSERT_EQUAL_STRING("value", d.parts[0].data.c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_single_part_upload);
    RUN_TEST(test_multi_part_mixed_fields);
    RUN_TEST(test_boundary_split_across_many_small_feeds);
    RUN_TEST(test_large_file_delivered_in_chunks_stays_bounded);
    RUN_TEST(test_preamble_before_first_boundary_is_ignored);
    return UNITY_END();
}
