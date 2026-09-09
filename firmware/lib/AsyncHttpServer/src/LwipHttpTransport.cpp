#include "LwipHttpTransport.h"

#ifndef UNIT_TEST

#include "HttpConnection.h"

LwipHttpTransport::LwipHttpTransport(tcp_pcb *pcb) : _pcb(pcb) {
    tcp_arg(_pcb, this);
    tcp_recv(_pcb, &LwipHttpTransport::sRecv);
    tcp_sent(_pcb, &LwipHttpTransport::sSent);
    tcp_err(_pcb, &LwipHttpTransport::sError);
    tcp_poll(_pcb, &LwipHttpTransport::sPoll, 1);
    tcp_setprio(_pcb, TCP_PRIO_MIN);
}

LwipHttpTransport::~LwipHttpTransport() {
    // If close() hasn't already run (e.g. the peer reset the connection),
    // make sure lwIP never calls back into a transport that's being freed.
    if (_pcb) {
        tcp_arg(_pcb, nullptr);
        tcp_recv(_pcb, nullptr);
        tcp_sent(_pcb, nullptr);
        tcp_err(_pcb, nullptr);
        tcp_poll(_pcb, nullptr, 0);
        tcp_abort(_pcb);
        _pcb = nullptr;
    }
    freePending();
}

void LwipHttpTransport::freePending() {
    if (_pendingHead) {
        pbuf_free(_pendingHead);
        _pendingHead = nullptr;
        _pendingLen = 0;
    }
}

void LwipHttpTransport::setConnection(HttpConnection *connection) {
    _connection = connection;
    if (!connection) {
        return;
    }

    // Flush anything buffered while we sat in the server's pending queue.
    if (_pendingHead) {
        pbuf *chain = _pendingHead;
        const size_t total = _pendingLen;
        _pendingHead = nullptr;
        _pendingLen = 0;
        for (pbuf *seg = chain; seg != nullptr; seg = seg->next) {
            _connection->onDataReceived(reinterpret_cast<const uint8_t *>(seg->payload), seg->len);
        }
        if (_pcb) {
            tcp_recved(_pcb, total);
        }
        pbuf_free(chain);
    }
}

size_t LwipHttpTransport::write(const uint8_t *data, size_t len) {
    if (!_pcb) {
        return 0;
    }

    uint16_t sndbuf = tcp_sndbuf(_pcb);
    if (sndbuf == 0) {
        return 0;  // send window full - caller retries once onSent()/onPoll() fires
    }

    size_t n = len < sndbuf ? len : sndbuf;
    err_t err = tcp_write(_pcb, data, static_cast<uint16_t>(n), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        return 0;  // ERR_MEM (buffer momentarily full) or a fatal error we can't act on here
    }

    tcp_output(_pcb);
    return n;
}

void LwipHttpTransport::close() {
    if (!_pcb) {
        return;
    }
    tcp_arg(_pcb, nullptr);
    tcp_recv(_pcb, nullptr);
    tcp_sent(_pcb, nullptr);
    tcp_err(_pcb, nullptr);
    tcp_poll(_pcb, nullptr, 0);

    tcp_pcb *pcb = _pcb;
    _pcb = nullptr;
    freePending();
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
    }
}

err_t LwipHttpTransport::onRecv(tcp_pcb *pcb, pbuf *pb, err_t /*err*/) {
    if (pb == nullptr) {
        // Peer closed its side. We never keep connections open past a full
        // response (always "Connection: close"), so treat this the same as
        // any other closure: acknowledge it and finish tearing down the pcb.
        tcp_arg(pcb, nullptr);
        tcp_recv(pcb, nullptr);
        tcp_sent(pcb, nullptr);
        tcp_err(pcb, nullptr);
        tcp_poll(pcb, nullptr, 0);
        _pcb = nullptr;
        freePending();
        if (tcp_close(pcb) != ERR_OK) {
            tcp_abort(pcb);
        }
        if (_connection) {
            _connection->onClosed();
        }
        return ERR_OK;
    }

    if (!_connection) {
        // Queued by the server waiting for a free slot: hold the pbuf chain
        // without tcp_recved() so the client stalls on TCP window
        // backpressure instead of us growing an unbounded buffer. `pb`
        // itself may be a multi-node chain (fragmented across several pool
        // buffers), so use pbuf_cat() to append correctly rather than
        // assuming pb is a single node.
        if (_pendingHead) {
            pbuf_cat(_pendingHead, pb);
        } else {
            _pendingHead = pb;
        }
        _pendingLen += pb->tot_len;
        return ERR_OK;
    }

    for (pbuf *seg = pb; seg != nullptr; seg = seg->next) {
        _connection->onDataReceived(reinterpret_cast<const uint8_t *>(seg->payload), seg->len);
    }
    tcp_recved(pcb, pb->tot_len);
    pbuf_free(pb);
    return ERR_OK;
}

err_t LwipHttpTransport::onSent(tcp_pcb * /*pcb*/, uint16_t /*len*/) {
    if (_connection) {
        _connection->onWritable();
    }
    return ERR_OK;
}

err_t LwipHttpTransport::onPoll(tcp_pcb * /*pcb*/) {
    if (_connection) {
        _connection->onPollTick();
    }
    return ERR_OK;
}

void LwipHttpTransport::onError(err_t /*err*/) {
    // lwIP has already freed the pcb by the time this fires - never touch it.
    _pcb = nullptr;
    freePending();
    if (_connection) {
        _connection->onClosed();
    }
}

err_t LwipHttpTransport::sRecv(void *arg, tcp_pcb *pcb, pbuf *pb, err_t err) {
    if (!arg) {
        return ERR_OK;
    }
    return reinterpret_cast<LwipHttpTransport *>(arg)->onRecv(pcb, pb, err);
}

err_t LwipHttpTransport::sSent(void *arg, tcp_pcb *pcb, uint16_t len) {
    if (!arg) {
        return ERR_OK;
    }
    return reinterpret_cast<LwipHttpTransport *>(arg)->onSent(pcb, len);
}

err_t LwipHttpTransport::sPoll(void *arg, tcp_pcb *pcb) {
    if (!arg) {
        return ERR_OK;
    }
    return reinterpret_cast<LwipHttpTransport *>(arg)->onPoll(pcb);
}

void LwipHttpTransport::sError(void *arg, err_t err) {
    if (!arg) {
        return;
    }
    reinterpret_cast<LwipHttpTransport *>(arg)->onError(err);
}

#endif  // UNIT_TEST
