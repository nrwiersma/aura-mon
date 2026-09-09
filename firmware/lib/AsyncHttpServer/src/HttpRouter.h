//
// HttpRouter - method/path route table. Pure logic, no socket dependency.
//

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "HttpRequest.h"
#include "HttpResponseProducer.h"
#include "HttpUploadHandler.h"

class HttpRouter {
public:
    using HandlerFn = std::function<std::unique_ptr<HttpResponseProducer>(const HttpRequest &)>;
    using UploadFactoryFn = std::function<std::unique_ptr<HttpUploadHandler>(const HttpRequest &)>;

    // Registers a handler for an exact method + path match.
    void on(HttpMethod method, const std::string &path, HandlerFn handler);

    // Registers a streaming upload route: instead of buffering the body,
    // the connection streams multipart/form-data parts directly into the
    // HttpUploadHandler the factory returns.
    void onUpload(HttpMethod method, const std::string &path, UploadFactoryFn factory);

    // Registers a fallback handler used when no route matches. If not set,
    // route() returns nullptr for unmatched requests (caller maps to 404).
    void onNotFound(HandlerFn handler);

    // Looks up and invokes the handler for the given request. Returns
    // nullptr if there is no matching route and no not-found handler set.
    std::unique_ptr<HttpResponseProducer> route(const HttpRequest &req) const;

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
