//
// Created by Nicholas Wiersma on 2026/01/24.
//

// Mock lwip inet functions
#define IPADDR_NONE 0xFFFFFFFF

inline uint32_t ipaddr_addr(const char *cp) {
    if (!cp || !*cp) return 0xFFFFFFFF; // IPADDR_NONE
    // Validate dotted-decimal IPv4: exactly 3 dots, each octet 0-255.
    int dots   = 0;
    int octet  = -1; // -1 means "no digit seen yet in this octet"
    const char *p = cp;
    while (*p) {
        if (*p == '.') {
            if (octet < 0 || octet > 255) return 0xFFFFFFFF;
            dots++;
            octet = -1;
        } else if (*p >= '0' && *p <= '9') {
            octet = (octet < 0 ? 0 : octet) * 10 + (*p - '0');
            if (octet > 255) return 0xFFFFFFFF;
        } else {
            return 0xFFFFFFFF; // invalid character
        }
        p++;
    }
    if (dots != 3 || octet < 0 || octet > 255) return 0xFFFFFFFF;
    return 0; // Valid IP (not IPADDR_NONE)
}
