minitelnet
==========

.. _introduction:

Introduction
------------

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

This makes the basic architecture:

.. code-block:: text

   transport -> telnet_feed() -> event handler -> application
                                        |
                                        +-> TELNET_EV_SEND -> transport

A transport can be a TCP socket, TLS stream, UART, pipe, file, or anything
else capable of carrying an ordered byte stream.

.. _getting_started:

Getting started
---------------

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

.. _receiving:

Receiving data
--------------

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

.. _sending:

Sending data
------------

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

.. _negotiation:

RFC 1143 option negotiation
---------------------------

Telnet options are negotiated independently in both directions:

.. code-block:: text

                      local option       peer option

   request enable     WILL               DO
   request disable    WONT               DONT
   peer enables       DO                 WILL
   peer disables      DONT               WONT

Repeated, simultaneous and crossed negotiations make it insufficient to
represent an option with a simple enabled/disabled flag. RFC 1143 defines
the Q method to handle these cases without negotiation loops.

minitelnet implements this state machine separately from the parser:

.. code-block:: c

   #include <minitelnet-negotiation.h>

   telnet_negotiation_t negotiation = { 0 };

Each direction of an option can be in one of the stable states NO or YES,
or in WANTNO/WANTYES while waiting for a reply. The OPPOSITE variants
represent a changed desired state while a negotiation is already pending.

For example:

.. code-block:: text

   NO
    |
    | request DO
    v
   WANTYES
    |
    | request DONT before reply
    v
   WANTYES_OPPOSITE
    |
    | receive WILL
    v
   WANTNO  -- send DONT -->
    |
    | receive WONT
    v
   NO

Incoming TELNET_EV_NEG events are passed explicitly to the state machine:

.. code-block:: c

   case TELNET_EV_NEG: {
       struct telnet_negotiation_transition trns;

       if (telnet_negotiation_feed(negotiation,
                                   event->command.code,
                                   event->neg.option,
                                   &trns)) {
           if (trns.outgoing)
               telnet_send_negotiation(telnet,
                                       trns.outgoing,
                                       trns.option);

           /* Handle the resulting state here. */
       }
       break;
   }

:c:func:`telnet_negotiation_request` expresses a desired option state. It does not
necessarily produce a command: requesting a state which is already active
or already pending is a no-op. If a command must be transmitted, it is
returned in ``trns.outgoing`` and encoded using :c:func:`telnet_send_negotiation`.

.. _negotiation_requests:

Negotiation Requests
^^^^^^^^^^^^^^^^^^^^

minitelnet adds :c:enumerator:`TELNET_OPTION_PENDING` for unsolicited enable
requests. This allows option policy to remain in the application.

Receiving DO while the local option is NO, or WILL while the peer option
is NO, enters REQUEST_PENDING. The application then accepts or rejects
the request with :c:func:`telnet_negotiation_respond`:

.. code-block:: text

   received DO:    WILL = accept, WONT = reject
   received WILL:  DO   = accept, DONT = reject

A response may be made immediately or later. If the peer withdraws the
request first, a later response is ignored and does not start a new
negotiation.

.. _subnegotiation:

Subnegotiation
--------------

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

.. _option_semantics:

Option semantics
----------------

The parser and negotiation state machine intentionally stop at generic
Telnet mechanics. They do not know what options such as BINARY, TTYPE,
NAWS, LINEMODE, or NEW-ENVIRON mean.

The optional ``minitelnet-def.h`` header provides constants for common Telnet
options and option-specific command values. Including that header does not
enable or implement those options.

This separation keeps minitelnet usable with both standard and private
Telnet options without placing option policy in the protocol engine.

.. _errors:

Errors and recovery
-------------------

Malformed or unexpected protocol sequences are reported as
:c:enumerator:`TELNET_EV_ERROR`. Error codes are described by :c:enum:`telnet_error`.
Errors are reported through the same synchronous event mechanism as other
parser events. The state machine recovers from protocol errors and remains
in a valid state, so whether a connection should be closed after an error is
an application decision.

.. _lifetime:

Lifetime and allocation
-----------------------

minitelnet performs no dynamic allocation for its protocol state. The
application owns the :c:struct:`telnet` object and must keep it alive while it is
in use.

Use :c:func:`telnet_reset` to return an existing instance to its initial parser
state while preserving its configured event handler and user data.
Alternatively, :c:func:`telnet_init` may be called on an existing instance to
fully reinitialize it with a new event handler and user data. This is useful
when reusing storage for a new connection.

As the state machine does not dynamically allocate any memory, no explicit
deallocation is required.

.. _reentrancy:

Event callbacks and reentrancy
------------------------------

Such calls may themselves cause further events, including
:c:enumerator:`TELNET_EV_SEND`, before the original callback returns. Event handlers
should therefore tolerate nested callbacks.

.. _integration:

Integration model
-----------------

minitelnet is intended to sit between an ordered byte transport and an
application protocol:

.. code-block:: text

   +-----------------------------+
   | application                 |
   | option policy and semantics |
   +-----------------------------+
                 |
   +-----------------------------+
   | minitelnet-negotiation      |
   | RFC 1143 state (optional)   |
   +-----------------------------+
                 |
   +-----------------------------+
   | minitelnet                  |
   | Telnet framing              |
   +-----------------------------+
                 |
   +-----------------------------+
   | transport                   |
   | TCP / TLS / UART / ...      |
   +-----------------------------+

TLS, socket management, reconnect logic, terminal emulation, PTYs, and
character encoding are intentionally outside the library.

See the examples shipped with the project for small complete programs
demonstrating the event loop, option negotiation, and subnegotiation.

.. _zephyr:

Zephyr
^^^^^^

minitelnet can be used directly as a Zephyr module. The repository contains
the module metadata and build integration required for importing it through
a west manifest. To enable minitelnet in a Zephyr application, enable the
following option:

.. code-block:: text

   CONFIG_MINITELNET=y

The library itself remains independent of Zephyr APIs; using it as a Zephyr
module only integrates minitelnet into the Zephyr build system. Applications
use the same public API described throughout this documentation.
