# Links the "IPv4 Only - 32K" lwIP variant (libipv4-big.a, prebuilt with
# __LWIP_MEMMULT=2) instead of the default libipv4.a. The default pool only
# has 5 TCP PCBs, which cannot fit our 6-slot AsyncHttpServer plus the
# listen PCB plus TIME_WAIT stragglers - exhaustion makes lwIP reset
# incoming connections. The matching -D__LWIP_MEMMULT=2 build flag keeps
# our own translation units consistent with the big archive's pool sizes.

Import("env")

import os

libs = env.get("LIBS", [])


def swap(lib):
    path = str(lib)
    if os.path.basename(path) != "libipv4.a":
        return lib
    big = os.path.join(os.path.dirname(path), "libipv4-big.a")
    if not os.path.isfile(big):
        print("use_lwip_big: WARNING: %s not found, keeping libipv4.a" % big)
        return lib
    print("use_lwip_big: linking %s" % big)
    return env.File(big)


env.Replace(LIBS=[swap(lib) for lib in libs])
