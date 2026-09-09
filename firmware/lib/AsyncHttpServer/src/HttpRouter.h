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

class HttpRouter {
public:
    using HandlerFn = std::function<std::unique_ptr<HttpResponseProducer>(const HttpRequest &)>;

    // Registers a handler for an exact method + path match.
    void on(HttpMethod method, const std::string &path, HandlerFn handler);

    // Registers a fallback handler used when no route matches. If not set,
    // route() returns nullptr for unmatched requests (caller maps to 404).
    void onNotFound(HandlerFn handler);

    // Looks up and invokes the handler for the given request. Returns
    // nullptr if there is no matching route and no not-found handler set.
    std::unique_ptr<HttpResponseProducer> route(const HttpRequest &req) const;

private:
    struct Route {
        HttpMethod method;
        std::string path;
        HandlerFn handler;
    };

    std::vector<Route> _routes;
    HandlerFn _notFound;
};
