#include "AsyncHttpServer.h"

#ifndef UNIT_TEST

#include <Arduino.h>

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

    tcp_pcb *listenPcb = tcp_listen_with_backlog(pcb, static_cast<uint8_t>(_slots.size() + kMaxPending));
    if (!listenPcb) {
        tcp_close(pcb);
        return false;
    }

    _listenPcb = listenPcb;
    tcp_arg(_listenPcb, this);
    tcp_accept(_listenPcb, &AsyncHttpServer::sAccept);
    return true;
}

// A client that connects but then stalls mid-request holds its slot
// indefinitely; close it out once it has been idle this long so the
// (small) pool can't be starved.
static constexpr uint32_t kIdleTimeoutMs = 10000;

void AsyncHttpServer::attach(Slot &slot, tcp_pcb *pcb) {
    slot.transport = std::make_unique<LwipHttpTransport>(pcb);
    slot.connection = std::make_unique<HttpConnection>(*slot.transport, _router);
    slot.transport->setConnection(slot.connection.get());
}

void AsyncHttpServer::drainPending() {
    const uint32_t now = millis();

    for (auto &slot : _slots) {
        if (slot.inUse()) {
            continue;
        }

        // Drop queued connections whose pcb died (peer reset / error) or
        // that waited too long before handing this slot the next one.
        while (!_pending.empty() &&
               (!_pending.front().transport->isOpen() ||
                now - _pending.front().queuedAtMs > kPendingTimeoutMs)) {
            _pending.pop_front();
        }
        if (_pending.empty()) {
            return;
        }

        slot.transport = std::move(_pending.front().transport);
        _pending.pop_front();
        slot.connection = std::make_unique<HttpConnection>(*slot.transport, _router);
        slot.transport->setConnection(slot.connection.get());  // flushes buffered bytes
    }
}

void AsyncHttpServer::reap() {
    const uint32_t now = millis();
    for (auto &slot : _slots) {
        if (!slot.inUse()) {
            continue;
        }
        // The sole close() call site: closing from inside an lwIP callback
        // resets the connection on this stack, so completions staged by the
        // recv/sent/poll callbacks are closed here, from loop context.
        slot.connection->closeIfDrained();
        if (slot.connection->isFinished()) {
            slot.connection.reset();
            slot.transport.reset();
            continue;
        }
        if (now - slot.connection->lastActivityMs() > kIdleTimeoutMs) {
            // Let the connection clean up (e.g. abort an in-flight upload),
            // then recycle the slot.
            slot.connection->onClosed();
            slot.transport->close();
            slot.connection.reset();
            slot.transport.reset();
        }
    }

    drainPending();
}

err_t AsyncHttpServer::onAccept(tcp_pcb *newpcb, err_t /*err*/) {
    for (auto &slot : _slots) {
        if (slot.inUse()) {
            continue;
        }

        attach(slot, newpcb);
        return ERR_OK;
    }

    // All slots busy: queue the pcb (it buffers its own request bytes via
    // TCP window backpressure) and service it once reap() frees a slot.
    // Only a queue overflow is a genuine overload worth refusing.
    if (_pending.size() < kMaxPending) {
        _pending.push_back(Pending{std::make_unique<LwipHttpTransport>(newpcb), millis()});
        return ERR_OK;
    }

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
