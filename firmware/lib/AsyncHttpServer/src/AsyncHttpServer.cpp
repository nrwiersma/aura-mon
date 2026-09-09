#include "AsyncHttpServer.h"

#ifndef UNIT_TEST

AsyncHttpServer::AsyncHttpServer(HttpRouter &router, uint16_t port, size_t maxConnections)
    : _router(router), _port(port), _slots(maxConnections) {}

AsyncHttpServer::~AsyncHttpServer() {
    if (_listenPcb) {
        tcp_close(_listenPcb);
    }
}

bool AsyncHttpServer::begin() {
    tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        return false;
    }

    pcb->so_options |= SOF_REUSEADDR;
    if (tcp_bind(pcb, IP_ANY_TYPE, _port) != ERR_OK) {
        tcp_close(pcb);
        return false;
    }

    tcp_pcb *listenPcb = tcp_listen_with_backlog(pcb, static_cast<uint8_t>(_slots.size()));
    if (!listenPcb) {
        tcp_close(pcb);
        return false;
    }

    _listenPcb = listenPcb;
    tcp_arg(_listenPcb, this);
    tcp_accept(_listenPcb, &AsyncHttpServer::sAccept);
    return true;
}

void AsyncHttpServer::reap() {
    for (auto &slot : _slots) {
        if (slot.inUse() && slot.connection->isFinished()) {
            slot.connection.reset();
            slot.transport.reset();
        }
    }
}

err_t AsyncHttpServer::onAccept(tcp_pcb *newpcb, err_t /*err*/) {
    for (auto &slot : _slots) {
        if (slot.inUse()) {
            continue;
        }

        slot.transport = std::make_unique<LwipHttpTransport>(newpcb);
        slot.connection = std::make_unique<HttpConnection>(*slot.transport, _router);
        slot.transport->setConnection(slot.connection.get());
        return ERR_OK;
    }

    // Pool exhausted: refuse the connection rather than let it queue behind
    // (and block) requests we can actually service.
    tcp_abort(newpcb);
    return ERR_ABRT;
}

err_t AsyncHttpServer::sAccept(void *arg, tcp_pcb *newpcb, err_t err) {
    if (!arg) {
        return ERR_OK;
    }
    return reinterpret_cast<AsyncHttpServer *>(arg)->onAccept(newpcb, err);
}

#endif  // UNIT_TEST
