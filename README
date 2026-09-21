minitelnet
==========

A small TELNET protocol implementation for C.

minitelnet handles IAC parsing and escaping, option negotiation using the
RFC 1143 Q method, and streaming subnegotiation. It does not implement
TELNET option semantics or perform network I/O.

The library is allocation-free and transport-independent. It targets especially
embedded environments. Output is reported through the event callback and can 
be written to a socket, UART, TLS stream, or another byte transport.

Build

    cmake -B build
    cmake --build build

Test

    ctest --test-dir build --output-on-failure

Zephyr

minitelnet can be imported as a Zephyr module using west.

Add the repository to your west manifest:

    manifest:
      projects:
        - name: minitelnet
          url: https://...
          revision: main

The module is then available to Zephyr applications through its CMake
integration. Enable the library with CONFIG_MINITELNET=y 
and include <minitelnet.h> as usual.

See the Zephyr documentation for adding external modules through west
manifests.

License

zlib. See LICENSE.
