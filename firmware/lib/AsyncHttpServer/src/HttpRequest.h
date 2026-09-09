//
// HttpRequest - the parsed result of an incoming request, produced
// incrementally by HttpRequestParser.
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class HttpMethod {
    UNKNOWN,
    GET,
    POST,
    PUT,
    DELETE,
    OPTIONS,
};

HttpMethod httpMethodFromString(const std::string &s);

struct HttpHeader {
    std::string name;  // lower-cased
    std::string value;
};

struct HttpQueryParam {
    std::string key;
    std::string value;
};

struct HttpRequest {
    HttpMethod method = HttpMethod::UNKNOWN;
    std::string path;                          // decoded, without query string
    std::vector<HttpQueryParam> query;
    std::vector<HttpHeader> headers;
    std::string body;

    // Returns the value of the first query parameter with the given key,
    // or nullptr if not present.
    const std::string *queryParam(const std::string &key) const {
        for (const auto &q : query) {
            if (q.key == key) {
                return &q.value;
            }
        }
        return nullptr;
    }

    // Returns the value of the first header with the given name (matched
    // case-insensitively against the caller-supplied lower-case name), or
    // nullptr if not present.
    const std::string *header(const std::string &lowerName) const {
        for (const auto &h : headers) {
            if (h.name == lowerName) {
                return &h.value;
            }
        }
        return nullptr;
    }
};
