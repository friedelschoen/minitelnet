Option negotiation
******************

Telnet options are negotiated independently in both directions:

+-----------------+--------------+-------------+
|                 | local option | peer option |
+=================+==============+=============+
| request enable  | ``WILL``     | ``DO``      |
+-----------------+--------------+-------------+
| request disable | ``WONT``     | ``DONT``    |
+-----------------+--------------+-------------+
| peer enables    | ``DO``       | ``WILL``    |
+-----------------+--------------+-------------+
| peer disables   | ``DONT``     | ``WONT``    |
+-----------------+--------------+-------------+

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

Negotiation Requests
====================

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

Option semantics
================

The parser and negotiation state machine intentionally stop at generic
Telnet mechanics. They do not know what options such as BINARY, TTYPE,
NAWS, LINEMODE, or NEW-ENVIRON mean.

The optional ``minitelnet-def.h`` header provides constants for common Telnet
options and option-specific command values. Including that header does not
enable or implement those options.

This separation keeps minitelnet usable with both standard and private
Telnet options without placing option policy in the protocol engine.


