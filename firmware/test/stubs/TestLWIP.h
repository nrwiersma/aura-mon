//
// Created by Nicholas Wiersma on 2026/01/24.
//

// Mock lwip inet functions
#define IPADDR_NONE 0xFFFFFFFF

inline uint32_t ipaddr_addr(const char *cp) {
    if (!cp || !*cp) return 0xFFFFFFFF; // IPADDR_NONE
    // Simple validation: must have at least one dot and only digits/dots
    const char *p = cp;
    int dots = 0;
    bool hasDigit = false;
    while (*p) {
        if (*p == '.') {
            dots++;
            if (!hasDigit) return 0xFFFFFFFF; // dot without preceding digit
            hasDigit = false;
        } else if (*p >= '0' && *p <= '9') {
            hasDigit = true;
        } else {
            return 0xFFFFFFFF; // invalid character
        }
        p++;
    }
    if (dots != 3 || !hasDigit) return 0xFFFFFFFF;
    return 0; // Valid IP (not IPADDR_NONE)
}
