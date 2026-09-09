//
// HttpRouter - method/path route table. Pure logic, no socket dependency.
// Handlers write their response through the HttpRequest response channel
// (req.send / req.hold+write+done) and return void.
//

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "HttpRequest.h"
#include "HttpUploadHandler.h"

class HttpRouter {
public:
    using HandlerFn = std::function<void(HttpRequest &)>;
    using UploadFactoryFn = std::function<std::unique_ptr<HttpUploadHandler>(HttpRequest &)>;

    // Registers a handler for an exact method + path match.
    void on(HttpMethod method, const std::string &path, HandlerFn handler);

    // Registers a streaming upload route: instead of buffering the body,
    // the connection streams multipart/form-data parts directly into the
    // HttpUploadHandler the factory returns.
    void onUpload(HttpMethod method, const std::string &path, UploadFactoryFn factory);

    // Registers a fallback handler used when no route matches. If not set,
    // route() answers 404 for unmatched requests.
    void onNotFound(HandlerFn handler);

    // Looks up and invokes the handler for the given request. Answers 404
    // when no route matches and no not-found handler is set.
    void route(HttpRequest &req) const;

    // Looks up an upload route without invoking it. Returns nullptr if
    // method + path don't match any upload route.
    const UploadFactoryFn *findUpload(HttpMethod method, const std::string &path) const;

private:
    struct Route {
        HttpMethod method;
        std::string path;
        HandlerFn handler;
    };

    struct UploadRoute {
        HttpMethod method;
        std::string path;
        UploadFactoryFn factory;
    };

    std::vector<Route> _routes;
    std::vector<UploadRoute> _uploadRoutes;
    HandlerFn _notFound;
};
