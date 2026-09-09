//
// LwipHttpTransport - HttpTransport implementation backed by a raw lwIP
// tcp_pcb. Bridges lwIP's tcp_recv/tcp_sent/tcp_poll/tcp_err callbacks to an
// attached HttpConnection so the connection is driven purely by socket
// readiness, never by anything that blocks or spins.
//

#pragma once

#ifndef UNIT_TEST

#include <cstddef>
#include <cstdint>

#include "lwip/tcp.h"

#include "HttpTransport.h"

class HttpConnection;

class LwipHttpTransport : public HttpTransport {
public:
    explicit LwipHttpTransport(tcp_pcb *pcb);
    ~LwipHttpTransport() override;

    // Must be called once the HttpConnection that owns this transport has
    // been constructed, so lwIP callbacks have somewhere to be delivered.
    // May be deferred: while no connection is attached, received bytes are
    // buffered (without tcp_recved, so TCP window backpressure caps the
    // buffer) and flushed into the connection here.
    void setConnection(HttpConnection *connection);

    size_t write(const uint8_t *data, size_t len) override;
    void close() override;
    bool isOpen() const override { return _pcb != nullptr; }

private:
    err_t onRecv(tcp_pcb *pcb, pbuf *pb, err_t err);
    err_t onSent(tcp_pcb *pcb, uint16_t len);
    err_t onPoll(tcp_pcb *pcb);
    void onError(err_t err);

    static err_t sRecv(void *arg, tcp_pcb *pcb, pbuf *pb, err_t err);
    static err_t sSent(void *arg, tcp_pcb *pcb, uint16_t len);
    static err_t sPoll(void *arg, tcp_pcb *pcb);
    static void sError(void *arg, err_t err);

    void freePending();

    tcp_pcb *_pcb;
    HttpConnection *_connection = nullptr;
    pbuf *_pendingHead = nullptr;  // recv'd while connection-less (queued); a
                                   // pbuf_cat()-linked chain, tail found by
                                   // walking ->next when needed
    size_t _pendingLen = 0;
};

#endif  // UNIT_TEST
