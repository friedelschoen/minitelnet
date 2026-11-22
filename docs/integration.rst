Integration
***********

Errors and recovery
===================

Malformed or unexpected protocol sequences are reported as
:c:enumerator:`TELNET_EV_ERROR`. Error codes are described by :c:enum:`telnet_error`.
Errors are reported through the same synchronous event mechanism as other
parser events. The state machine recovers from protocol errors and remains
in a valid state, so whether a connection should be closed after an error is
an application decision.

Lifetime and allocation
=======================

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

Event callbacks and reentrancy
==============================

Such calls may themselves cause further events, including :c:enumerator:`TELNET_EV_SEND`, 
before the original callback returns. Event handlers should therefore tolerate nested callbacks.

Integration model
=================

minitelnet is intended to sit between an ordered byte transport and an
application protocol:

.. code-block:: text

       +-------------------------------------------+
       | application                               |
       | option policy and semantics               |
       +-------------------------------------------+
                   |                       |
     +--minitelnet-----------------------------------+
     |             |                       |         |
     | +........................+ +................+ |
     | . minitelnet-negotiation . . minitelnet-def . |
     | . RFC 1143 state         . . Options        . |
     | +........................+ +................+ |
     |             |                                 |
     | +-------------------------------------------+ |
     | | minitelnet                                | |
     | | Telnet framing                            | | 
     | +-------------------------------------------+ |
     +-----------------------------------------------+
                            |
       +-------------------------------------------+
       | transport                                 |
       | TCP / TLS / UART / ...                    |
       +-------------------------------------------+

TLS, socket management, reconnect logic, terminal emulation, PTYs, and
character encoding are intentionally outside the library.

See the examples shipped with the project for small complete programs
demonstrating the event loop, option negotiation, and subnegotiation.


Zephyr
======

minitelnet can be used directly as a Zephyr module. The repository contains
the module metadata and build integration required for importing it through
a west manifest. To enable minitelnet in a Zephyr application, enable the
following option::

   CONFIG_MINITELNET=y

The library itself remains independent of Zephyr APIs; using it as a Zephyr
module only integrates minitelnet into the Zephyr build system. Applications
use the same public API described throughout this documentation.


Complete example walkthrough
============================

This walkthrough demonstrates a minimal Telnet application using standard input
and output as its transport. Bytes received from ``stdin`` are passed to
minitelnet, while outgoing Telnet bytes are written to ``stdout``.

The example does not establish a network connection or implement any
Telnet option semantics. More complete examples can be found in the
``examples/`` directory.

Initialization
~~~~~~~~~~~~~~

The basic initialization of minitelnet looks like the following:

.. code-block:: c
   :linenos:

   struct telnet telnet;
   unsigned char buffer[1024];
   size_t n;

   telnet_init(&telnet, handler, NULL);

   while ((n = fread(buffer, 1, sizeof(buffer), stdin)) > 0)
       telnet_feed(&telnet, buffer, n);

First, a :c:struct:`telnet` object is allocated on the stack. This object
does not need to be zero-initialized, as :c:func:`telnet_init` initializes
its internal state.

The initializer receives three arguments:

* **telnet**: Pointer to the Telnet state object.
* **handler**: Callback invoked whenever an event occurs.
* **userdata**: Optional application-defined pointer, here ``NULL``.

The loop continuously reads bytes from ``stdin`` into ``buffer``.
Whenever data is received, :c:func:`telnet_feed` processes those bytes
and updates the parser state.

Input does not need to align with Telnet commands or subnegotiations.
The parser preserves its state between calls, so an incomplete command
can be continued by the next input chunk.

Note that :c:func:`telnet_feed` does not produce any transport output
directly. Instead, outgoing bytes are reported through the registered
event handler.

Event Handling
~~~~~~~~~~~~~~

The event handler is the central interface between minitelnet and the
application. It receives all parser events synchronously, including
incoming application data, commands, option negotiations and outgoing
protocol bytes.

The following handler demonstrates the basic event types:

.. code-block:: c
   :linenos:

   void handler(struct telnet *telnet,
                const union telnet_event *event,
                void *userdata)
   {
       size_t i;

       (void)telnet;
       (void)userdata;

       switch (event->type) {
       case TELNET_EV_SEND:
           fwrite(event->data.buffer, 1, event->data.size, stdout);
           fflush(stdout);
           break;

       case TELNET_EV_COMMAND:
           fprintf(stderr, "telnet: command %02x issued\n",
                   event->command.code);
           break;

       case TELNET_EV_ERROR:
           fprintf(stderr, "telnet: error %02x\n",
                   event->error.code);
           break;

       case TELNET_EV_DATA:
           fprintf(stderr, "telnet: data:");

           for (i = 0; i < event->data.size; i++)
               fprintf(stderr, " %02x", event->data.buffer[i]);

           fprintf(stderr, "\n");
           break;

       default:
           break;
       }
   }

The handler receives three arguments, as defined by
:c:type:`telnet_handler_t`:

* **telnet**: The Telnet state machine that emitted the event.
* **event**: The event and its associated payload.
* **userdata**: The application-defined pointer supplied during initialization.

Events are represented by :c:union:`telnet_event`. Its ``type`` member
identifies the active event, while the remaining members provide
event-specific information.

All possible event types are defined in :c:enum:`telnet_event_type`.

Sending Output
^^^^^^^^^^^^^^

The most important event for transport integration is
:c:enumerator:`TELNET_EV_SEND`. It indicates that minitelnet has
generated bytes that must be transmitted to the peer.

In this example, the bytes are written directly to ``stdout``:

.. code-block:: c

   fwrite(event->data.buffer, 1, event->data.size, stdout);
   fflush(stdout);

These bytes already contain the required Telnet framing and escaping.
They must therefore be transmitted without modification.

In a network application, this operation would typically write to a
socket or enqueue the bytes in an outgoing transport buffer.

A single call to a minitelnet send function may produce multiple
:c:enumerator:`TELNET_EV_SEND` events. Applications must not assume
a one-to-one relationship between API calls and transport writes.

.. note::

   This example assumes that writing to ``stdout`` succeeds. Real
   transports, especially non-blocking sockets, may accept only part
   of the supplied data. The application must handle partial writes,
   buffering and transport errors itself.

Receiving Data
^^^^^^^^^^^^^^

Ordinary application data is delivered through
:c:enumerator:`TELNET_EV_DATA`.

The payload is available through ``event->data.buffer`` and its length
through ``event->data.size``.

In this example, each received byte is printed in hexadecimal form to
``stderr``. Using ``stderr`` for diagnostics keeps those messages
separate from the Telnet byte stream written to ``stdout``.

Telnet framing and escaped IAC bytes have already been processed by
the parser. The application receives the resulting payload bytes.

Data may arrive in arbitrary-sized chunks, and a single call to
:c:func:`telnet_feed` can produce multiple data events.

.. important::

   Event buffers are borrowed and remain valid only for the duration
   of the callback. Applications that need to retain received data
   must copy it before returning from the handler.
   

Commands and Errors
^^^^^^^^^^^^^^^^^^^

Standalone Telnet commands are reported through
:c:enumerator:`TELNET_EV_COMMAND`.

Their command code is available through ``event->command.code``.
The application can interpret these commands or ignore them if they
are not relevant.

Malformed protocol sequences produce
:c:enumerator:`TELNET_EV_ERROR`, with an error code defined by
:c:enum:`telnet_error`.

The parser attempts to recover from malformed input and remains in
a valid state. The application decides whether to continue processing
or terminate the connection.


Option Negotiation
~~~~~~~~~~~~~~~~~~

Option negotiation is deliberately not covered in this walkthrough.
The negotiation state machine and its request handling are described in
:doc:`negotiation`.

Subnegotiation
~~~~~~~~~~~~~~

Some Telnet options exchange additional information using
subnegotiation sequences.

These are reported through :c:enumerator:`TELNET_EV_SUBNEG`.

Like ordinary application data, subnegotiation payloads are delivered
incrementally rather than buffered as complete messages.

The option number is available through ``event->subneg.option``, while
the payload is accessed through ``event->data``.

For example:

.. code-block:: c

   case TELNET_EV_SUBNEG:
       if (event->data.buffer == NULL) {
           fprintf(stderr, "telnet: subnegotiation ended\n");
       } else {
           fprintf(stderr, "telnet: received %lu bytes for option %u\n",
                   (unsigned long)event->data.size,
                   (unsigned int)event->subneg.option);
       }
       break;

The end of a subnegotiation is indicated by a final event with a
``NULL`` buffer and a size of zero.

The application can process payload chunks immediately or assemble
them into a complete message when required by the option protocol.

Sending Data
~~~~~~~~~~~~

Until now, this example has only processed received bytes and
transmitted protocol output requested by minitelnet.

Applications can also send their own data using
:c:func:`telnet_send_data`:

.. code-block:: c

   static const unsigned char message[] = "Hello, Telnet!\r\n";

   telnet_send_data(&telnet, message, sizeof(message) - 1);

This function applies the necessary Telnet escaping and emits one or
more :c:enumerator:`TELNET_EV_SEND` events.

The event handler then writes those encoded bytes to ``stdout``.

It is important to distinguish :c:func:`telnet_send_data` from writing
directly to the transport. Application data must pass through minitelnet
so that reserved protocol bytes, particularly IAC, are escaped correctly.

minitelnet does not implement option-specific character encoding or
line-ending transformations. Applications must handle those rules
according to their negotiated Telnet options.

Event Lifetime and Reentrancy
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

All minitelnet events are delivered synchronously.

The event handler is called while :c:func:`telnet_feed` or one of
the sending functions is executing. Once the handler returns, the
application must no longer rely on pointers into the event payload.

The handler may also invoke minitelnet sending functions. Such calls
can immediately generate additional events before the original handler
has returned.

For example, calling :c:func:`telnet_send_data` from a
:c:enumerator:`TELNET_EV_DATA` handler may cause a nested
:c:enumerator:`TELNET_EV_SEND` callback.

Applications must therefore account for nested event callbacks and
avoid relying on event buffers after their callback has completed.
