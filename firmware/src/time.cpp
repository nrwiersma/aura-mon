//
// Created by Nicholas Wiersma on 2025/10/01.
//

#include "auramon.h"

#define ntpPort 2390

const String ntpSrvs[] = {"time.google.com", "time.aws.com", "time.cloudflare.com"};

struct ntp_timestamp_t {
    uint32_t seconds;
    uint32_t fraction;
};

#pragma pack(push, 1)
struct ntpPacket {
    uint8_t flags;
    uint8_t stratum;
    int8_t poll;
    int8_t precision;
    int32_t rootDelay;
    uint32_t rootDispersion;
    uint8_t refClockIdent[4];
    ntp_timestamp_t refTS;
    ntp_timestamp_t originateTS;
    ntp_timestamp_t recvTS;
    ntp_timestamp_t transmitTS;

    ntpPacket() : flags(0xE3),
                  stratum(0),
                  poll(6),
                  precision(0xEC),
                  rootDelay(0),
                  rootDispersion(0),
                  refClockIdent{49, 0x4E, 49, 52},
                  refTS{0, 0},
                  originateTS{0, 0},
                  recvTS{0, 0},
                  transmitTS{0, 0} {
    };
};
#pragma pack(pop)

void timestamp_ntoh(ntp_timestamp_t *t) {
    t->seconds = ntohl(t->seconds);
    t->fraction = ntohl(t->fraction);
}

uint32_t timeSync(void *param) {
    (void) param;

    static uint8_t srvIdx = 0;
    WiFiUDP udp;

    if (millis() > 3628800000UL) {
        // The millisecond monotonic clock will roll over every 49 days.
        // Restart every 42 days to be safe.
        LOGI("timeSync: Restarting to reset monotonic clock");
        rp2040.reboot();
    }

    if (!eth.isLinked() || !eth.connected()) {
        // Try again when ethernet is connected.
        return rtcRunning ? 60 : 5;
    }

    String srv = ntpSrvs[srvIdx++ % sizeof(ntpSrvs)];
    IPAddress srvIP;
    if (!eth.hostByName(srv.c_str(), srvIP)) {
        return rtcRunning ? 60 : 5;
    }

    uint32_t sentTS = millis();
    ntp_timestamp_t origTS = {sentTS / 1000, sentTS % 1000};
    ntpPacket pkt;
    pkt.originateTS = origTS;

    udp.begin(ntpPort);
    udp.beginPacket(srvIP, 123);
    udp.write(reinterpret_cast<uint8_t *>(&pkt), sizeof(ntpPacket)); // send an NTP packet to a time server
    udp.endPacket();

    // Wait for the NTP reply.
    while (!udp.parsePacket()) {
        if (millis() - sentTS > (rtcRunning ? 3000 : 10000)) {
            udp.stop();
            return rtcRunning ? 60 : 5;
        }
    }

    uint32_t recvTS = millis();
    size_t pktSize = udp.read(reinterpret_cast<uint8_t *>(&pkt), sizeof(ntpPacket));
    udp.stop();

    if (pktSize < sizeof(ntpPacket)) {
        return rtcRunning ? 60 : 5;
    }
    if (pkt.stratum == 0) {
        LOGE("timeSync: Time server sent kiss-o-death packet: code=%c%c%c%c, ip=%s",
             pkt.refClockIdent[0], pkt.refClockIdent[1], pkt.refClockIdent[2], pkt.refClockIdent[3],
             srvIP.toString().c_str()
        );
        return rtcRunning ? 60 : 15;
    }

    timestamp_ntoh(&pkt.transmitTS);
    pkt.transmitTS.fraction /= 4294967UL; // Convert from fraction to ms.
    uint32_t dur = recvTS - sentTS;
    struct timeval tv;
    tv.tv_sec = (pkt.transmitTS.seconds + (pkt.transmitTS.fraction + dur / 2) / 1000) - 2208988800UL;
    tv.tv_usec = (pkt.transmitTS.fraction + dur / 2) % 1000;
    settimeofday(&tv, nullptr);

    rtc.adjust(tv.tv_sec);
    if (!rtcRunning) {
        rtc.start();
        rtcRunning = true;
    }

    LOGI("timeSync: RTC adjusted to Unix time %d", tv.tv_sec);

    return 3600 * 1000;
}
