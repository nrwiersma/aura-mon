#include "HttpRequestParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

}  // namespace

HttpMethod httpMethodFromString(const std::string &s) {
    if (s == "GET") return HttpMethod::GET;
    if (s == "POST") return HttpMethod::POST;
    if (s == "PUT") return HttpMethod::PUT;
    if (s == "DELETE") return HttpMethod::DELETE;
    if (s == "OPTIONS") return HttpMethod::OPTIONS;
    return HttpMethod::UNKNOWN;
}

void HttpRequestParser::reset() {
    _state = State::RequestLine;
    _buf.clear();
    _consumed = 0;
    _req = HttpRequest();
    _contentLength = 0;
    _bodyBytesRemaining = 0;
    _onHeadersComplete = nullptr;
    _bodySink = nullptr;
    _errorReason = nullptr;
}

HttpParseStatus HttpRequestParser::fail(const char *reason) {
    _state = State::Error;
    _errorReason = reason;
    return HttpParseStatus::Error;
}

HttpParseStatus HttpRequestParser::feed(const uint8_t *data, size_t len) {
    if (_state == State::Complete || _state == State::Error) {
        // Ignore further data until reset().
        return _state == State::Complete ? HttpParseStatus::Complete : HttpParseStatus::Error;
    }

    _buf.append(reinterpret_cast<const char *>(data), len);

    if (_state == State::RequestLine || _state == State::Headers) {
        if (_buf.size() - _consumed > kMaxHeaderBytes) {
            return fail("headers too large");
        }
    }

    return pump();
}

bool HttpRequestParser::consumeLine(std::string &line) {
    size_t pos = _buf.find("\r\n", _consumed);
    if (pos == std::string::npos) {
        return false;
    }
    line = _buf.substr(_consumed, pos - _consumed);
    _consumed = pos + 2;
    return true;
}

bool HttpRequestParser::parseRequestLine(const std::string &line) {
    size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos) {
        return false;
    }
    size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) {
        return false;
    }

    std::string method = line.substr(0, sp1);
    std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    // Ignore the HTTP version token (line.substr(sp2 + 1)).

    _req.method = httpMethodFromString(method);
    if (_req.method == HttpMethod::UNKNOWN) {
        return false;
    }

    size_t qpos = target.find('?');
    if (qpos == std::string::npos) {
        _req.path = urlDecode(target);
    } else {
        _req.path = urlDecode(target.substr(0, qpos));
        parseQueryString(target.substr(qpos + 1), _req);
    }
    return !_req.path.empty() && _req.path[0] == '/';
}

bool HttpRequestParser::parseHeaderLine(const std::string &line) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    HttpHeader h;
    h.name = toLower(trim(line.substr(0, colon)));
    h.value = trim(line.substr(colon + 1));
    _req.headers.push_back(std::move(h));
    return true;
}

void HttpRequestParser::parseQueryString(const std::string &qs, HttpRequest &req) {
    size_t start = 0;
    while (start <= qs.size()) {
        size_t amp = qs.find('&', start);
        std::string pair = qs.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!pair.empty()) {
            size_t eq = pair.find('=');
            HttpQueryParam p;
            if (eq == std::string::npos) {
                p.key = urlDecode(pair);
            } else {
                p.key = urlDecode(pair.substr(0, eq));
                p.value = urlDecode(pair.substr(eq + 1));
            }
            req.query.push_back(std::move(p));
        }
        if (amp == std::string::npos) {
            break;
        }
        start = amp + 1;
    }
}

std::string HttpRequestParser::urlDecode(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            char hex[3] = {s[i + 1], s[i + 2], 0};
            out += static_cast<char>(std::strtol(hex, nullptr, 16));
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

HttpParseStatus HttpRequestParser::pump() {
    while (true) {
        switch (_state) {
            case State::RequestLine: {
                std::string line;
                if (!consumeLine(line)) {
                    return HttpParseStatus::NeedMoreData;
                }
                if (!parseRequestLine(line)) {
                    return fail("malformed request line");
                }
                _state = State::Headers;
                break;
            }
            case State::Headers: {
                std::string line;
                if (!consumeLine(line)) {
                    return HttpParseStatus::NeedMoreData;
                }
                if (line.empty()) {
                    // Blank line: end of headers.
                    _contentLength = 0;
                    if (const std::string *cl = _req.header("content-length")) {
                        char *end = nullptr;
                        long v = std::strtol(cl->c_str(), &end, 10);
                        if (end == cl->c_str() || v < 0) {
                            return fail("malformed content-length");
                        }
                        _contentLength = static_cast<size_t>(v);
                    }
                    _bodyBytesRemaining = _contentLength;
                    _state = State::Body;
                    if (_onHeadersComplete) {
                        // May call streamBodyTo() before any body byte is consumed.
                        _onHeadersComplete(_req);
                    }
                    if (!_bodySink && _contentLength > kMaxBodyBytes) {
                        return fail("body too large");
                    }
                    break;
                }
                if (!parseHeaderLine(line)) {
                    return fail("malformed header line");
                }
                break;
            }
            case State::Body: {
                size_t available = _buf.size() - _consumed;
                if (_bodySink) {
                    size_t take = available < _bodyBytesRemaining ? available : _bodyBytesRemaining;
                    if (take > 0) {
                        _bodySink(reinterpret_cast<const uint8_t *>(_buf.data() + _consumed), take);
                        _consumed += take;
                        _bodyBytesRemaining -= take;
                    }
                    // Drop everything already handed to the sink so the
                    // buffer doesn't grow across an arbitrarily long body.
                    _buf.erase(0, _consumed);
                    _consumed = 0;
                    if (_bodyBytesRemaining > 0) {
                        return HttpParseStatus::NeedMoreData;
                    }
                    _state = State::Complete;
                    return HttpParseStatus::Complete;
                }
                if (available < _contentLength) {
                    return HttpParseStatus::NeedMoreData;
                }
                _req.body = _buf.substr(_consumed, _contentLength);
                _consumed += _contentLength;
                _state = State::Complete;
                return HttpParseStatus::Complete;
            }
            case State::Complete:
                return HttpParseStatus::Complete;
            case State::Error:
                return HttpParseStatus::Error;
        }
    }
}
