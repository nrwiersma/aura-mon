//
// Created by Nicholas Wiersma on 2026/09/10.
//

#include "HTTPRequest.h"

void HTTPRequest::sendHeader(const String &name, const String &value, bool first) {
    String headerLine = name;
    headerLine += F(": ");
    headerLine += value;
    headerLine += "\r\n";

    if (first) {
        _responseHeaders = headerLine + _responseHeaders;
    } else {
        _responseHeaders += headerLine;
    }
}

void HTTPRequest::_prepareHeader(String &response, int code, const char *content_type) {
    response = String(F("HTTP/1.1 "));
    response += String(code);
    response += ' ';
    response += _responseCodeToString(code);
    response += "\r\n";

    if (!content_type) {
        content_type = "text/html";
    }

    sendHeader(String(F("Content-Type")), String(FPSTR (content_type)), true);
    sendHeader(String(F("Accept-Ranges")), String(F("none")));
    if (code != 204 && code != 304) {
        sendHeader(String(F("Transfer-Encoding")), String(F("chunked")));
        _chunked = true;
    }
    if (_corsEnabled) {
        sendHeader(String(FPSTR ("Access-Control-Allow-Origin")), String("*"));
        sendHeader(String(FPSTR ("Access-Control-Allow-Methods")), String("*"));
        sendHeader(String(FPSTR ("Access-Control-Allow-Headers")), String("*"));
    }
    sendHeader(String(F("Connection")), String(F("close")));

    response += _responseHeaders;
    response += "\r\n";
    _responseHeaders = "";
    _headerSent = true;
}

void HTTPRequest::send(int code, const char *content_type, const String &content) {
    String header;
    _prepareHeader(header, code, content_type);
    _client->write(header.c_str(), header.length());
    if (content.length()) {
        sendContent(content);
    }
}

void HTTPRequest::send(int code, char *content_type, const String &content) {
    send(code, (const char *) content_type, content);
}

void HTTPRequest::send(int code, const String &content_type, const String &content) {
    send(code, (const char *) content_type.c_str(), content);
}

void HTTPRequest::send(int code, const char *content_type, const char *content) {
    String header;
    _prepareHeader(header, code, content_type);
    _client->write(header.c_str(), header.length());
    const size_t contentLength = content ? strlen(content) : 0;
    if (contentLength) {
        sendContent(content, contentLength);
    }
}

void HTTPRequest::send_P(int code, PGM_P content_type, PGM_P content) {
    String header;
    _prepareHeader(header, code, content_type);
    _client->write(header.c_str(), header.length());
    const size_t contentLength = content ? strlen_P(content) : 0;
    if (contentLength) {
        sendContent_P(content, contentLength);
    }
}

void HTTPRequest::sendContent(const String &content) {
    sendContent(content.c_str(), content.length());
}

void HTTPRequest::sendContent(const char *content, size_t contentLength) {
    const char *footer = "\r\n";
    char        chunkSize[11];
    sprintf(chunkSize, "%x%s", (unsigned int) contentLength, footer);
    _client->print(chunkSize);

    _client->write(content, contentLength);

    _client->write(footer, 2);
}

void HTTPRequest::sendContent_P(PGM_P content) {
    sendContent_P(content, strlen_P(content));
}

void HTTPRequest::sendContent_P(PGM_P content, size_t size) {
    const char *footer = "\r\n";
    char        chunkSize[11];
    sprintf(chunkSize, "%x%s", (unsigned int) size, footer);
    _client->write(chunkSize, strlen(chunkSize));

    _client->write(content, size);

    _client->write(footer, 2);
}

String HTTPRequest::urlDecode(const String &text) {
    String       decoded = "";
    char         temp[] = "0x00";
    unsigned int len = text.length();
    unsigned int i = 0;
    while (i < len) {
        char decodedChar;
        char encodedChar = text.charAt(i++);
        if ((encodedChar == '%') && (i + 1 < len)) {
            temp[2] = text.charAt(i++);
            temp[3] = text.charAt(i++);

            decodedChar = strtol(temp, nullptr, 16);
        } else {
            if (encodedChar == '+') {
                decodedChar = ' ';
            } else {
                decodedChar = encodedChar; // normal ascii char
            }
        }
        decoded += decodedChar;
    }
    return decoded;
}

void HTTPRequest::_parseArguments(const String &query) {
    int start = 0;
    if (start < (int) query.length() && query[start] == '?') start++;

    while (start < (int) query.length()) {
        int amp = query.indexOf('&', start);
        if (amp == -1) amp = query.length();

        String pair = query.substring(start, amp);
        int    eq = pair.indexOf('=');

        String key, value;
        if (eq == -1) {
            key = pair;
        } else {
            key = pair.substring(0, eq);
            value = pair.substring(eq + 1);
        }

        // reuse WebServer's own decoder rather than reimplementing it
        key = urlDecode(key);
        value = urlDecode(value);

        if (key.length()) _params[key] = value;
        start = amp + 1;
    }
}

String HTTPRequest::_responseCodeToString(int code) {
    switch (code) {
        case 100: return F("Continue");
        case 101: return F("Switching Protocols");
        case 200: return F("OK");
        case 201: return F("Created");
        case 202: return F("Accepted");
        case 203: return F("Non-Authoritative Information");
        case 204: return F("No Content");
        case 205: return F("Reset Content");
        case 206: return F("Partial Content");
        case 300: return F("Multiple Choices");
        case 301: return F("Moved Permanently");
        case 302: return F("Found");
        case 303: return F("See Other");
        case 304: return F("Not Modified");
        case 305: return F("Use Proxy");
        case 307: return F("Temporary Redirect");
        case 400: return F("Bad Request");
        case 401: return F("Unauthorized");
        case 402: return F("Payment Required");
        case 403: return F("Forbidden");
        case 404: return F("Not Found");
        case 405: return F("Method Not Allowed");
        case 406: return F("Not Acceptable");
        case 407: return F("Proxy Authentication Required");
        case 408: return F("Request Time-out");
        case 409: return F("Conflict");
        case 410: return F("Gone");
        case 411: return F("Length Required");
        case 412: return F("Precondition Failed");
        case 413: return F("Request Entity Too Large");
        case 414: return F("Request-URI Too Large");
        case 415: return F("Unsupported Media Type");
        case 416: return F("Requested range not satisfiable");
        case 417: return F("Expectation Failed");
        case 500: return F("Internal Server Error");
        case 501: return F("Not Implemented");
        case 502: return F("Bad Gateway");
        case 503: return F("Service Unavailable");
        case 504: return F("Gateway Time-out");
        case 505: return F("HTTP Version not supported");
        default:  return F("");
    }
}
