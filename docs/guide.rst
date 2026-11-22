Usage Guide
***********

minitelnet is a small, allocation-free Telnet protocol engine for C. It
handles the protocol mechanics needed to turn a byte stream into Telnet
events and to encode outgoing Telnet data:

- IAC parsing and escaping;
- Telnet commands;
- optional RFC 1143 Q-method negotiation state;
- streaming subnegotiation;
- transport-independent input and output.

The library deliberately does not implement the semantics of individual
Telnet options and does not perform network I/O. An application decides
which options it supports, interprets their subnegotiation payloads, and
moves bytes between minitelnet and its transport.

This makes the basic architecture::

   transport -> telnet_feed() -> event handler -> application
       ^                              |
       +------ TELNET_EV_SEND <-------+

A transport can be a TCP socket, TLS stream, UART, pipe, file, or anything
else capable of carrying an ordered byte stream.

Getting started
===============

A minitelnet session is represented by :c:struct:`telnet`. Initialize it with
:c:func:`telnet_init` and provide an event handler of type :c:type:`telnet_handler_t`.
Received bytes are passed to :c:func:`telnet_feed`. The parser processes them
immediately and reports results synchronously through the configured event
handler.

The most important event when integrating a transport is :c:enumerator:`TELNET_EV_SEND`.
Whenever this event is emitted, the application must write the supplied bytes
to the peer. The library never writes to a socket or other transport itself.
Events and their payloads are described by :c:enum:`telnet_event_type` and
:c:union:`telnet_event`.

Option negotiation is handled by an optional, independent RFC 1143 state
machine in ``minitelnet-negotiation.h``. The core parser only reports
WILL/WONT/DO/DONT commands and performs their wire encoding.

Receiving data
==============

Pass data received from the peer directly to :c:func:`telnet_feed`. Input does
not need to align with Telnet commands, negotiations, or subnegotiations.
The parser keeps the required state between calls. An IAC byte may therefore
be received in one call and its command byte in another.

Ordinary application bytes are emitted as :c:enumerator:`TELNET_EV_DATA`. Telnet
framing and escaped IAC bytes have already been processed. Applications
should not assume that one call to :c:func:`telnet_feed` produces one data event:
payload may be emitted in arbitrary-sized chunks.

Event buffers are borrowed and valid only for the duration of the callback.
Copy data that must be retained after the handler returns. No data is buffered
internally and the application must implement buffering by itself if I/O-
operations are expensive.

Sending data
============

Use :c:func:`telnet_send_data` for ordinary application bytes. The library
performs Telnet-level escaping, including escaping literal IAC bytes, and
reports encoded output through :c:enumerator:`TELNET_EV_SEND`.

Calling a send function does not itself perform I/O. A single operation may
produce more than one :c:enumerator:`TELNET_EV_SEND` event, so applications should
treat these events as a stream rather than expect a one-to-one mapping
between API calls and transport writes.

Standalone Telnet commands can be generated with :c:func:`telnet_send_command`.
Option negotiation and subnegotiation have dedicated interfaces described
below.

minitelnet only performs Telnet protocol framing. Application-level rules
associated with negotiated options remain the responsibility of the caller.

Subnegotiation
==============

Option-specific protocols commonly exchange data inside Telnet SB/SE
subnegotiations. minitelnet handles the SB/SE framing and IAC escaping, but
deliberately does not interpret the payload.

Incoming payload is reported through :c:enumerator:`TELNET_EV_SUBNEG`. Subnegotiation
is streamed: a single SB/SE sequence may produce any number of events.
Consequently, the library does not need an internal buffer large enough for
the complete payload. Its successful end is reported by a final
:c:enumerator:`TELNET_EV_SUBNEG` with ``event->data.buffer`` == NULL and
``event->data.size`` == 0.

:c:struct:`telnet_event_subneg` identifies the option. The payload chunk is
available through ``event->data``. Applications that require a complete message
can assemble the chunks themselves; applications with streaming option
protocols can process them immediately.

Outgoing subnegotiation works in the same manner. Calls to
:c:func:`telnet_send_subnegotiation` for the same option append payload to the
currently open subnegotiation. Use :c:func:`telnet_send_subnegotiation_end` to
terminate it explicitly.

Starting a subnegotiation for another option closes the previous one.
Sending ordinary application data with :c:func:`telnet_send_data` also returns
the sender to normal data mode.

Explicit termination is particularly important when two consecutive
subnegotiations for the same option must remain separate; otherwise another
call for the same option continues the currently open subnegotiation.


Example
=======

This minimal example uses standard input and output as the transport.
It reads incoming Telnet bytes from ``stdin`` and sends encoded bytes to
``stdout`` through the event handler. It does not establish a network
connection or implement option negotiation.

.. code-block:: c
   :linenos:

   #include <minitelnet.h>
   #include <stdio.h>

   static void handler(struct telnet *telnet,
                       const union telnet_event *event, void *userdata)
   {
       (void)userdata;

       switch (event->type) {
       case TELNET_EV_SEND:
           fwrite(event->data.buffer, 1, event->data.size, stdout);
           fflush(stdout);
           break;
       case TELNET_EV_DATA:
           telnet_send_data(telnet, event->data.buffer, event->data.size);
           break;
       default:
           break;
       }
   }

   int main(void)
   {
       struct telnet telnet;
       unsigned char buffer[1024];
       size_t n;

       telnet_init(&telnet, handler, NULL);

       while ((n = fread(buffer, 1, sizeof(buffer), stdin)) > 0)
           telnet_feed(&telnet, buffer, n);

       return ferror(stdin) ? 1 : 0;
   }

:c:func:`telnet_init` initializes the :c:struct:`telnet` object and registers
an event handler. Received bytes are parsed with :c:func:`telnet_feed`.
The handler echoes :c:enumerator:`TELNET_EV_DATA` using
:c:func:`telnet_send_data`, while :c:enumerator:`TELNET_EV_SEND` writes
encoded output to ``stdout``.

See :doc:`integration` for a detailed walkthrough of initialization, events,
transport output and callback lifetime. Option negotiation is covered
separately in :doc:`negotiation`.
