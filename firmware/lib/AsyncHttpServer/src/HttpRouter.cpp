#include "HttpRouter.h"

void HttpRouter::on(HttpMethod method, const std::string &path, HandlerFn handler) {
    _routes.push_back(Route{method, path, std::move(handler)});
}

void HttpRouter::onUpload(HttpMethod method, const std::string &path, UploadFactoryFn factory) {
    _uploadRoutes.push_back(UploadRoute{method, path, std::move(factory)});
}

void HttpRouter::onNotFound(HandlerFn handler) {
    _notFound = std::move(handler);
}

std::unique_ptr<HttpResponseProducer> HttpRouter::route(const HttpRequest &req) const {
    for (const auto &r : _routes) {
        if (r.method == req.method && r.path == req.path) {
            return r.handler(req);
        }
    }
    if (_notFound) {
        return _notFound(req);
    }
    return nullptr;
}

const HttpRouter::UploadFactoryFn *HttpRouter::findUpload(HttpMethod method, const std::string &path) const {
    for (const auto &r : _uploadRoutes) {
        if (r.method == method && r.path == path) {
            return &r.factory;
        }
    }
    return nullptr;
}
