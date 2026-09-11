//
// Created by Nicholas Wiersma on 2026/09/10.
//

#pragma once

#include <HTTPServer.h>
#include <map>

class HTTPRequest {
public:
    using ContentTypeFunction = HTTPServer::ContentTypeFunction;

    HTTPRequest(const String &      method, const String &url, const String &paramsString, WiFiClient *client,
                ContentTypeFunction contentType)
        : _method(method),
          _url(url),
          _client(client),
          _contentType(contentType),
          _corsEnabled(false) {
        _parseArguments(paramsString);
    }

    ~HTTPRequest() {
        if (_headerSent && !_finished) finish();
        if (_ownsClient) delete _client;
    }

    const String &method() const { return _method; }
    const String &url() const { return _url; }

    bool hasArg(const String &name) const {
        return _params.find(name) != _params.end();
    }

    String arg(const String &name) const {
        auto it = _params.find(name);
        return it == _params.end() ? String() : it->second;
    }

    void send(int code, const char *content_type = nullptr, const String &content = String(""));
    void send(int code, char *content_type, const String &content);
    void send(int code, const String &content_type, const String &content);
    void send(int code, const char *content_type, const char *content);

    void send_P(int code, PGM_P content_type, PGM_P content);

    template<typename TypeName>
    void send(int code, PGM_P content_type, TypeName content, size_t contentLength) {
        send(code, content_type, (const char *) content, contentLength);
    }

    void enableCORS(bool value = true) { _corsEnabled = value; }
    void takeClientOwnership() { _ownsClient = true; }

    void sendHeader(const String &name, const String &value, bool first = false);
    void sendContent(const String &content);
    void sendContent(const char *content, size_t contentLength);
    void sendContent_P(PGM_P content);
    void sendContent_P(PGM_P content, size_t size);

    static String urlDecode(const String &text);

    // Call when done sending (also happens automatically in the
    // destructor as a fallback). Writes the terminating 0-length chunk.
    void finish() {
        if (_finished) return;
        if (_chunked) {
            _client->print(F("0\r\n\r\n"));
        }
        _client->flush();
        _client->stop();
        _finished = true;
    }

protected:
    void _prepareHeader(String &response, int code, const char *content_type);
    void _parseArguments(const String &data);
    static String _responseCodeToString(int code);

    String                   _method;
    String                   _url;
    std::map<String, String> _params;
    WiFiClient *             _client;
    ContentTypeFunction      _contentType;

    bool _corsEnabled;
    bool _ownsClient = false;

    String _responseHeaders;

    bool _headerSent = false;
    bool _chunked = false;
    bool _finished = false;
};
