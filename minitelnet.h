/* SPDX-License-Identifier: Zlib */
/* Copyright (c) 2026 Friedel Schön */

#pragma once

/**
 * @file minitelnet.h
 * @brief Streaming Telnet protocol parser and encoder.
 */

#include <stddef.h>

/**
 * @defgroup misc_group Miscellaneous Definitions
 * @{
 */

/** Telnet Interpret As Command (IAC) byte. */
#define TELNET_IAC 0xff

/**
 * Telnet protocol commands.
 *
 * Commands are transmitted after an IAC byte. WILL, WONT, DO and DONT
 * are followed by an option byte, while SB starts a subnegotiation.
 */
enum telnet_command {
	TELNET_CMD_SE = 0xf0,   /**< End subnegotiation. */
	TELNET_CMD_NOP = 0xf1,  /**< No operation. */
	TELNET_CMD_DM = 0xf2,   /**< Data mark. */
	TELNET_CMD_BRK = 0xf3,  /**< Break. */
	TELNET_CMD_IP = 0xf4,   /**< Interrupt process. */
	TELNET_CMD_AO = 0xf5,   /**< Abort output. */
	TELNET_CMD_AYT = 0xf6,  /**< Are You There. */
	TELNET_CMD_EC = 0xf7,   /**< Erase character. */
	TELNET_CMD_EL = 0xf8,   /**< Erase line. */
	TELNET_CMD_GA = 0xf9,   /**< Go Ahead. */
	TELNET_CMD_SB = 0xfa,   /**< Begin subnegotiation. */
	TELNET_CMD_WILL = 0xfb, /**< Sender offers/enables an option. */
	TELNET_CMD_WONT = 0xfc, /**< Sender refuses/disables an option. */
	TELNET_CMD_DO = 0xfd,   /**< Sender requests/enables an option at the peer. */
	TELNET_CMD_DONT = 0xfe, /**< Sender refuses/disables an option at the peer. */
	TELNET_CMD_ESC = 0xff   /**< Escaped IAC byte. */
};

/** Errors detected while parsing the Telnet stream. */
enum telnet_error {
	/** SB was encountered while a subnegotiation was already active. */
	TELNET_ERR_INVALID_SB,

	/** SE was encountered while no subnegotiation was active. */
	TELNET_ERR_INVALID_SE,
};

/** @} */

/**
 * @defgroup event_group Event Definitions
 * @{
 */

/**
 * Events emitted by the Telnet state machine.
 *
 * Events are delivered synchronously through telnet_handler_t while
 * processing input or generating output.
 */
enum telnet_event_type {
	/**
	 * A Telnet command without library-defined semantics was received.
	 *
	 * The command is available as event->command.code. This event is used for
	 * commands such as NOP, DM, BRK, IP, AO, AYT, EC, EL and GA.
	 *
	 * Unknown command codes are also reported through this event, allowing
	 * applications to implement private commands. Unknown commands are treated
	 * as single-byte commands without arguments. For example, the following
	 * sequence emits one command event with the command code 0xA0:
	 *
	 *     IAC A0
	 */
	TELNET_EV_COMMAND,

	/**
	 * Bytes must be written to the peer.
	 *
	 * The bytes are available through event->data. The library does not
	 * perform socket or transport I/O itself.
	 *
	 * Output is streaming and this event may be emitted multiple times for
	 * a single telnet_send_*() operation.
	 *
	 * The buffer is only valid for the duration of the callback.
	 */
	TELNET_EV_SEND,

	/**
	 * A WILL, WONT, DO or DONT option negotiation command was received.
	 *
	 * event->neg contains the received command and option number.
	 *
	 * This event reports the negotiation command as it appeared on the wire.
	 * No option negotiation state or policy is maintained by the Telnet parser.
	 * Applications may handle the command directly or pass it to an RFC 1143
	 * negotiation state machine.
	 */
	TELNET_EV_NEG,

	/**
	 * A chunk of subnegotiation payload was received.
	 *
	 * event->subneg.option identifies the Telnet option. Payload is
	 * delivered incrementally through event->data; the complete
	 * subnegotiation is deliberately not buffered by the library.
	 *
	 * The end of the subnegotiation is reported by a final event with
	 * event->data.buffer == NULL and event->data.size == 0.
	 */
	TELNET_EV_SUBNEG,

	/**
	 * A chunk of ordinary Telnet application data was received.
	 *
	 * Telnet commands and IAC escaping have already been removed. Payload
	 * may be delivered in arbitrary-sized chunks.
	 *
	 * The buffer is only valid for the duration of the callback.
	 */
	TELNET_EV_DATA,

	/**
	 * A malformed Telnet sequence was encountered.
	 *
	 * The specific error is available as event->error.code.
	 */
	TELNET_EV_ERROR
};

/**
 * A contiguous chunk of streamed data.
 *
 * The pointed-to buffer is borrowed from the library or caller and must not
 * be retained after the event handler returns.
 */
struct telnet_event_data {
	enum telnet_event_type _type; /**< @private */
	const unsigned char *buffer;  /**< First byte of this chunk. */
	size_t size;                  /**< Number of bytes in this chunk. */
};

/**
 * A contiguous chunk of subnegotiation payload.
 *
 * A subnegotiation may result in any number of TELNET_EV_SUBNEG events.
 * Consequently, receiving arbitrarily large subnegotiations does not
 * require an equally large internal buffer.
 *
 * The end of a subnegotiation is indicated by a final TELNET_EV_SUBNEG
 * event with data.buffer == NULL and data.size == 0.
 *
 * The streaming fields should be accessed through event->data;
 * event->subneg is only needed to access the option code.
 */
struct telnet_event_subneg {
	struct telnet_event_data _data; /**< @private */
	unsigned char option;           /**< Option to which this subnegotiation belongs. */
};

/**
 * A Telnet protocol error.
 *
 * This event reports malformed or unexpected protocol input. The parser
 * recovers from the error and remains in a valid state; deciding whether
 * to terminate the connection is left to the application.
 */
struct telnet_event_error {
	enum telnet_event_type _type; /**< @private */
	enum telnet_error code;       /**< Error that occurred. */
};

/**
 * A Telnet command without library-defined semantics.
 *
 * This event is used for commands such as NOP, DM, BRK, IP, AO, AYT, EC,
 * EL, and GA. Unknown command codes are also reported through this event,
 * allowing applications to implement private single-byte commands.
 */
struct telnet_event_command {
	enum telnet_event_type _type; /**< @private */
	enum telnet_command code;     /**< Telnet command code. */
};

/**
 * A received Telnet option negotiation command.
 *
 * The command is available through event->command.code and is one of
 * WILL, WONT, DO or DONT. option contains the following option byte.
 */
struct telnet_event_negotiate {
	struct telnet_event_command _command; /**< @private */
	unsigned char option;                 /**< Telnet option number. */
};

/**
 *
 * Event emitted by the Telnet state machine.
 *
 * All event structures have the event type as their first member, allowing
 * the active event type to be inspected through event->type.
 *
 * TELNET_EV_SUBNEG additionally shares its initial layout with
 * struct telnet_event_data, allowing its streaming payload to be accessed
 * through event->data.
 *
 * The active member is determined by event->type:
 *
 * - TELNET_EV_COMMAND: event->command
 * - TELNET_EV_SEND:    event->data
 * - TELNET_EV_NEG:     event->neg
 * - TELNET_EV_SUBNEG:  event->subneg, with streaming data through event->data
 * - TELNET_EV_DATA:    event->data
 * - TELNET_EV_ERROR:   event->error
 */
union telnet_event {
	enum telnet_event_type type;
	struct telnet_event_error error;
	struct telnet_event_data data;
	struct telnet_event_subneg subneg;
	struct telnet_event_negotiate neg;
	struct telnet_event_command command;
};

/** @} */

struct telnet;

/**
 * @defgroup telnet_group Telnet State Machine
 * @{
 */

/**
 * Telnet event callback.
 *
 * @param telnet   Telnet state that emitted the event.
 * @param event    Event-specific payload. Only valid during this call.
 * @param userdata Opaque pointer supplied to telnet_init().
 *
 * The handler is invoked synchronously while parsing input or generating
 * encoded output.
 *
 * TELNET_EV_SEND contains bytes that must be written to the underlying
 * transport. Other events report application data, commands,
 * subnegotiations, option negotiation commands and parser errors.
 */
typedef void (*telnet_handler_t)(struct telnet *telnet,
                                 const union telnet_event *event,
                                 void *userdata);

/**
 * Telnet parser and encoder state.
 *
 * Applications should initialize this structure with telnet_init() and
 * otherwise treat its underscore-prefixed members as private implementation
 * details.
 *
 * The structure contains only framing and streaming state. In particular,
 * it does not maintain Telnet option negotiation state or policy.
 *
 * No dynamic allocation is required.
 */
struct telnet {
	/** @private Event handler */
	telnet_handler_t _handler;

	/** @private Arbitrary data set by telnet_init() passed to the handler */
	void *_userdata;

	/** @private Current receive parser state. */
	int _state;

	/** @private WILL/WONT/DO/DONT currently being parsed, when applicable. */
	enum telnet_command _command;

	/** @private Active incoming subnegotiation option, or -1 if none is active. */
	int _recv_sub_option;

	/** @private Active outgoing subnegotiation option, or -1 if none is active. */
	int _send_sub_option;
};

/**
 * Initialize a Telnet state.
 *
 * @param telnet   State object to initialize.
 * @param handler  Callback receiving protocol events.
 * @param userdata Opaque application pointer passed unchanged to handler.
 *
 * The caller owns the telnet structure and must keep it alive for as long
 * as it is used. Initialization performs no transport I/O.
 */
void telnet_init(struct telnet *telnet, telnet_handler_t handler, void *userdata);

/**
 * Reset the Telnet parser and encoder state.
 *
 * The configured event handler and user data are preserved. Any partial
 * incoming command or subnegotiation and any active outgoing
 * subnegotiation are discarded.
 *
 * Option negotiation state, if maintained separately by the application,
 * is unaffected.
 *
 * @param telnet Telnet state.
 */
void telnet_reset(struct telnet *telnet);

/**
 * Feed received transport data into the Telnet parser.
 *
 * @param telnet Telnet state.
 * @param data   Bytes received from the peer.
 * @param size   Number of bytes in data.
 *
 * Application data, negotiations, commands and subnegotiation chunks are
 * reported synchronously through the event handler.
 */
void telnet_feed(struct telnet *telnet, const unsigned char *data, size_t size);

/**
 * Send application data to the peer.
 *
 * @param telnet Telnet state.
 * @param data   Application bytes to send.
 * @param size   Number of bytes to send.
 *
 * The library performs Telnet escaping as required, including escaping
 * literal IAC bytes. Encoded output is emitted through TELNET_EV_SEND.
 *
 * If a subnegotiation was sent previously, this call will end the subnegotiation
 * and continue sending regular data.
 *
 * Reminder that this library does not handle encoding states like Telnet BINARY
 * therefore if this option is not yet negotiated, line-endings and encoding
 * must be handled by the sender.
 */
void telnet_send_data(struct telnet *telnet, const unsigned char *data, size_t size);

/**
 * Send a chunk of subnegotiation payload.
 *
 * @param telnet Telnet state.
 * @param option Telnet option being subnegotiated.
 * @param data   Payload bytes.
 * @param size   Number of payload bytes.
 *
 * If no outgoing subnegotiation is active, this begins one for @p option.
 * Additional calls for the same option append more payload without requiring
 * the complete subnegotiation to be buffered.
 *
 * The subnegotiation remains open until
 * telnet_send_subnegotiation_end() or telnet_send_data() is called. If another
 * subnegotiation with a different option is active, this subnegotiation is ended before.
 *
 * Any required IAC escaping is performed by the library. Encoded bytes are
 * emitted through TELNET_EV_SEND.
 */
void telnet_send_subnegotiation(struct telnet *telnet, unsigned char option,
                                const unsigned char *data, size_t size);

/**
 * End an outgoing subnegotiation.
 *
 * @param telnet Telnet state.
 * @param option Option whose subnegotiation is being terminated.
 *
 * Emits the Telnet IAC SE sequence and returns the sender to normal data
 * mode.
 *
 * Explicit termination is necessary when two consecutive
 * subnegotiations for the same option must remain distinct. A subsequent
 * telnet_send_subnegotiation() call for the same option otherwise appends
 * to the currently open subnegotiation.
 *
 * The supplied option must match the currently active outgoing
 * subnegotiation otherwise it ignores the request.
 */
void telnet_send_subnegotiation_end(struct telnet *telnet, unsigned char option);

/**
 * Send a Telnet command.
 *
 * @param telnet  Telnet state.
 * @param command Command to send.
 *
 * This function is intended for standalone commands such as NOP, BRK, IP,
 * AO or AYT. Option negotiation should use telnet_send_negotiation(), and
 * subnegotiations should use telnet_send_subnegotiation().
 *
 * Encoded bytes are emitted through TELNET_EV_SEND.
 */
void telnet_send_command(struct telnet *telnet, enum telnet_command command);

/**
 * Encode and send a Telnet option negotiation command.
 *
 * @param telnet  Telnet state.
 * @param command TELNET_CMD_WILL, TELNET_CMD_WONT,
 *                TELNET_CMD_DO or TELNET_CMD_DONT.
 * @param option  Telnet option number.
 *
 * Emits IAC followed by @p command and @p option through TELNET_EV_SEND.
 *
 * This function performs no option negotiation state transition or policy
 * handling. It only encodes the supplied command.
 */
void telnet_send_negotiation(struct telnet *telnet, enum telnet_command command, unsigned char option);

/** @} */
