#pragma once

#include <stddef.h>
#include <stdint.h>

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
	TELNET_CMD_ESC = 0xff,  /**< Escaped IAC byte. */
};

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
	 * The command is available as event->command. This event is used for
	 * commands such as NOP, DM, BRK, IP, AO, AYT, EC, EL and GA.
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
	 * The peer requests a change to one of our options.
	 *
	 * event->neg.option identifies the option and event->neg.command is
	 * TELNET_CMD_DO or TELNET_CMD_DONT.
	 *
	 * Option policy is deliberately left to the application. The
	 * application may answer using telnet_send_negotiate() with WILL or
	 * WONT as appropriate.
	 */
	TELNET_EV_NEG_REQUEST,

	/**
	 * The peer responds to, or otherwise changes, its side of an option.
	 *
	 * event->neg.option identifies the option. event->neg.command contains
	 * the received WILL, WONT, DO or DONT command as applicable to the
	 * negotiation state.
	 *
	 * The library tracks negotiation state but does not assign semantics
	 * to individual Telnet options.
	 */
	TELNET_EV_NEG_RESPONSE,

	/**
	 * A chunk of subnegotiation payload was received.
	 *
	 * event->subneg.option identifies the Telnet option. Payload is
	 * delivered incrementally; the complete subnegotiation is deliberately
	 * not buffered by the library.
	 *
	 * event->subneg.offset gives the byte offset of this chunk within the
	 * current subnegotiation.
	 *
	 * The buffer is only valid for the duration of the callback.
	 */
	TELNET_EV_SUBNEG,

	/**
	 * A chunk of ordinary Telnet application data was received.
	 *
	 * Telnet commands and IAC escaping have already been removed. Payload
	 * may be delivered in arbitrary-sized chunks.
	 *
	 * event->data.offset gives the byte offset in the current input stream
	 * according to the library's offset semantics.
	 *
	 * The buffer is only valid for the duration of the callback.
	 */
	TELNET_EV_DATA,

	/**
	 * A malformed Telnet sequence was encountered.
	 *
	 * The specific error is available as event->error.
	 */
	TELNET_EV_ERROR,
};

/** Errors detected while parsing the Telnet stream. */
enum telnet_error {
	/** SB was encountered while a subnegotiation was already active. */
	TELNET_ERROR_INVALID_SB,

	/** SE was encountered while no subnegotiation was active. */
	TELNET_ERROR_INVALID_SE,
};

/**
 * State of one direction of a Telnet option negotiation.
 *
 * The additional transitional states allow simultaneous and outstanding
 * negotiations to be represented without assigning option-specific policy
 * to the library.
 *
 * WANT_* indicates a state change requested by the local endpoint and
 * awaiting a response from the peer.
 *
 * REQUEST_* indicates a state change requested by the peer and awaiting
 * a policy decision from the local application.
 */
enum telnet_option_state {
	TELNET_OPTION_DISABLED,
	TELNET_OPTION_ENABLED,
	TELNET_OPTION_WANT_DISABLED,
	TELNET_OPTION_WANT_ENABLED,
	TELNET_OPTION_REQUEST_DISABLED,
	TELNET_OPTION_REQUEST_ENABLED,
};

/**
 * Negotiation state for a Telnet option.
 *
 * `local` describes our side of the option and `peer` describes the peer's
 * side. Values are members of enum telnet_option_state.
 */
struct telnet_option {
	uint8_t local;
	uint8_t peer;
};

/**
 * A contiguous chunk of streamed data.
 *
 * The pointed-to buffer is borrowed from the library or caller and must not
 * be retained after the event handler returns.
 */
struct telnet_event_data {
	const uint8_t *buffer; /**< First byte of this chunk. */
	size_t size;           /**< Number of bytes in this chunk. */
	size_t offset;         /**< Stream-relative offset of the first byte. */
};

/**
 * A contiguous chunk of subnegotiation payload.
 *
 * A subnegotiation may result in any number of TELNET_EV_SUBNEG events.
 * Consequently, receiving arbitrarily large subnegotiations does not
 * require an equally large internal buffer.
 *
 * The initial members intentionally match struct telnet_event_data,
 * allowing a TELNET_EV_SUBNEG event to also be accessed through
 * event->data.
 */
struct telnet_event_subneg {
	const uint8_t *buffer; /**< Payload bytes in this chunk. */
	size_t size;           /**< Number of payload bytes in this chunk. */
	size_t offset;         /**< Offset within the current subnegotiation. */
	uint8_t option;        /**< Option to which this subnegotiation belongs. */
};

/** Information associated with a Telnet option negotiation event. */
struct telnet_event_negotiate {
	enum telnet_command command; /**< WILL, WONT, DO or DONT. */
	uint8_t option;              /**< Telnet option number. */
};

/**
 * Event payload passed to a telnet_handler_t.
 *
 * The active member is determined by enum telnet_event_type:
 *
 * - TELNET_EV_COMMAND: event.command
 * - TELNET_EV_SEND:    event.data
 * - TELNET_EV_NEG_*:   event.neg
 * - TELNET_EV_SUBNEG:  event.subneg or event.data
 * - TELNET_EV_DATA:    event.data
 * - TELNET_EV_ERROR:   event.error
 */
union telnet_event {
	enum telnet_error error;
	struct telnet_event_data data;
	struct telnet_event_subneg subneg;
	struct telnet_event_negotiate neg;
	enum telnet_command command;
};

struct telnet;

/**
 * Telnet event callback.
 *
 * @param telnet   Telnet state that emitted the event.
 * @param type     Type of event being delivered.
 * @param event    Event-specific payload. Only valid during this call.
 * @param userdata Opaque pointer supplied to telnet_init().
 *
 * The handler is invoked synchronously. For TELNET_EV_SEND, the application
 * should forward event->data.buffer to the underlying transport.
 */
typedef void (*telnet_handler_t)(struct telnet *telnet,
                                 enum telnet_event_type type,
                                 const union telnet_event *event,
                                 void *userdata);

/**
 * Telnet protocol state.
 *
 * Applications should normally initialize this structure with telnet_init()
 * and otherwise treat its underscore-prefixed members as considered private
 * implementation details and tent to change in future version.
 *
 * No dynamic allocation is required by the Telnet state itself.
 */
struct telnet {
	telnet_handler_t _handler;
	void *_userdata;

	/* Current receive parser state. */
	int _state;

	/* WILL/WONT/DO/DONT currently being parsed, when applicable. */
	enum telnet_command _command;

	/* Active incoming subnegotiation option, or -1 if none is active. */
	int _recv_sub_option;

	/* Current receive offset used for streaming events. */
	size_t _recv_offset;

	/* Active outgoing subnegotiation option, or -1 if none is active. */
	int _send_sub_option;

	/* Negotiation state for all 256 possible Telnet options. */
	struct telnet_option options[256];
};

/**
 * Initialize a Telnet state.
 *
 * @param telnet   State object to initialize.
 * @param handler  Callback receiving protocol events.
 * @param userdata Opaque application pointer passed unchanged to handler.
 *
 * The caller owns the telnet structure and must keep it alive for as long
 * as it is used. Initialization performs no network I/O and does not
 * automatically initiate option negotiation.
 */
void telnet_init(struct telnet *telnet, telnet_handler_t handler, void *userdata);

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
void telnet_feed(struct telnet *telnet, const uint8_t *data, size_t size);

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
 */
void telnet_send_data(struct telnet *telnet, const uint8_t *data, size_t size);

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
void telnet_send_subnegotiation(struct telnet *telnet, uint8_t option,
                                const uint8_t *data, size_t size);

/**
 * End an outgoing subnegotiation.
 *
 * @param telnet Telnet state.
 * @param option Option whose subnegotiation is being terminated.
 *
 * Emits the Telnet IAC SE sequence and returns the sender to normal data
 * mode.
 *
 * This explicit call is only required when you do intend to send the multiple
 * subnegotiations with the same option-code sequential.
 *
 * The supplied option must match the currently active outgoing
 * subnegotiation.
 */
void telnet_send_subnegotiation_end(struct telnet *telnet, uint8_t option);

/**
 * Send a Telnet command.
 *
 * @param telnet  Telnet state.
 * @param command Command to send.
 *
 * This function is intended for standalone commands such as NOP, BRK, IP,
 * AO or AYT. Option negotiation should use telnet_send_negotiate(), and
 * subnegotiations should use telnet_send_subnegotiation().
 *
 * Encoded bytes are emitted through TELNET_EV_SEND.
 */
void telnet_send_command(struct telnet *telnet,
                         enum telnet_command command);

/**
 * Initiate or respond to Telnet option negotiation.
 *
 * @param telnet  Telnet state.
 * @param command One of TELNET_CMD_WILL, TELNET_CMD_WONT,
 *                TELNET_CMD_DO or TELNET_CMD_DONT.
 * @param option  Telnet option number.
 *
 * The library manages the generic negotiation state but deliberately does
 * not determine whether a particular option should be accepted. Option
 * policy belongs to the application.
 *
 * Encoded negotiation bytes are emitted through TELNET_EV_SEND.
 */
void telnet_send_negotiate(struct telnet *telnet,
                           enum telnet_command command,
                           uint8_t option);
