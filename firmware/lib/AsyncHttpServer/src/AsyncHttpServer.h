//
// AsyncHttpServer - listens on a TCP port and drives up to a fixed number
// of concurrent HttpConnections purely from lwIP callbacks (accept/recv/
// sent/poll). Never blocks; nothing pumps anything. Extra connections
// beyond the pool size are rejected immediately so one slow client can
// never starve the others.
//

#pragma once

#ifndef UNIT_TEST

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "lwip/tcp.h"

#include "HttpConnection.h"
#include "HttpRouter.h"
#include "LwipHttpTransport.h"

class AsyncHttpServer {
public:
    // `maxConnections` bounds how many requests can be in flight at once;
    // size it to the number of clients you actually expect concurrently
    // (e.g. a couple of browser tabs / monitoring scripts), not to any
    // hardware socket limit - lwIP's PCB pool is what actually matters.
    AsyncHttpServer(HttpRouter &router, uint16_t port, size_t maxConnections = 4);
    ~AsyncHttpServer();

    AsyncHttpServer(const AsyncHttpServer &) = delete;
    AsyncHttpServer &operator=(const AsyncHttpServer &) = delete;

    // Binds and starts listening. Returns false on failure (e.g. port
    // already bound). Safe to retry.
    bool begin();

    // Reclaims slots for connections that have finished responding and
    // closed. Call once per loop() iteration; does no socket I/O itself -
    // that all already happened in lwIP callbacks - this just recycles
    // memory so a new connection can be accepted into a freed slot.
    void reap();

private:
    struct Slot {
        std::unique_ptr<LwipHttpTransport> transport;
        std::unique_ptr<HttpConnection> connection;

        bool inUse() const { return connection != nullptr; }
    };

    err_t onAccept(tcp_pcb *newpcb, err_t err);
    static err_t sAccept(void *arg, tcp_pcb *newpcb, err_t err);

    HttpRouter &_router;
    uint16_t _port;
    tcp_pcb *_listenPcb = nullptr;
    std::vector<Slot> _slots;
};

#endif  // UNIT_TEST
