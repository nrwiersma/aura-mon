#include "MultipartParser.h"

#include <algorithm>
#include <cctype>

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Extracts the value of `attr="..."` from a Content-Disposition header
// value (e.g. name="file"; filename="firmware.bin"). Returns "" if absent.
std::string extractAttr(const std::string &value, const std::string &attr) {
    std::string needle = attr + "=\"";
    size_t pos = value.find(needle);
    if (pos == std::string::npos) {
        return "";
    }
    pos += needle.size();
    size_t end = value.find('"', pos);
    if (end == std::string::npos) {
        return "";
    }
    return value.substr(pos, end - pos);
}

}  // namespace

MultipartParser::MultipartParser(std::string boundary, Delegate &delegate)
    : _delimiter("\r\n--" + boundary), _firstDelimiter("--" + boundary), _delegate(delegate) {}

void MultipartParser::feed(const uint8_t *data, size_t len) {
    if (_state == State::Done || _state == State::Error) {
        return;
    }
    _buf.append(reinterpret_cast<const char *>(data), len);
    processBuffered();
}

void MultipartParser::handleHeaderBlock(const std::string &block) {
    std::string name;
    std::string filename;

    size_t start = 0;
    while (start <= block.size()) {
        size_t eol = block.find("\r\n", start);
        std::string line = block.substr(start, eol == std::string::npos ? std::string::npos : eol - start);
        if (!line.empty()) {
            std::string lower = toLower(line);
            if (lower.rfind("content-disposition:", 0) == 0) {
                name = extractAttr(line, "name");
                filename = extractAttr(line, "filename");
            }
        }
        if (eol == std::string::npos) {
            break;
        }
        start = eol + 2;
    }

    _delegate.onPartBegin(name, filename);
}

void MultipartParser::processBuffered() {
    while (true) {
        switch (_state) {
            case State::Preamble: {
                size_t pos = _buf.find(_firstDelimiter);
                if (pos == std::string::npos) {
                    // Keep only a small tail in case the delimiter is split
                    // across chunks; anything before it is discardable
                    // preamble, not a part's data.
                    size_t keep = _firstDelimiter.size() > 0 ? _firstDelimiter.size() - 1 : 0;
                    if (_buf.size() > keep) {
                        _buf.erase(0, _buf.size() - keep);
                    }
                    return;
                }
                _buf.erase(0, pos + _firstDelimiter.size());
                _state = State::PartHeaders;
                break;
            }

            case State::PartHeaders: {
                if (_buf.size() < 2) {
                    return;  // need more data to tell "--" (final) from "\r\n" (headers)
                }
                if (_buf[0] == '-' && _buf[1] == '-') {
                    _state = State::Done;
                    return;
                }
                size_t blank = _buf.find("\r\n\r\n");
                if (blank == std::string::npos) {
                    if (_buf.size() > kMaxPartHeaderBytes) {
                        _state = State::Error;
                        return;
                    }
                    return;  // need more data
                }
                // `block` starts with the boundary line's trailing "\r\n"
                // then one header line per "\r\n"; handleHeaderBlock skips
                // the leading empty token itself.
                handleHeaderBlock(_buf.substr(0, blank));
                _buf.erase(0, blank + 4);
                _inPart = true;
                _state = State::PartData;
                break;
            }

            case State::PartData: {
                size_t pos = _buf.find(_delimiter);
                if (pos == std::string::npos) {
                    size_t keep = _delimiter.size() > 0 ? _delimiter.size() - 1 : 0;
                    if (_buf.size() > keep) {
                        _delegate.onPartData(reinterpret_cast<const uint8_t *>(_buf.data()), _buf.size() - keep);
                        _buf.erase(0, _buf.size() - keep);
                    }
                    return;
                }
                if (pos > 0) {
                    _delegate.onPartData(reinterpret_cast<const uint8_t *>(_buf.data()), pos);
                }
                _delegate.onPartEnd();
                _inPart = false;
                _buf.erase(0, pos + _delimiter.size());
                _state = State::PartHeaders;
                break;
            }

            case State::Done:
            case State::Error:
                return;
        }
    }
}
