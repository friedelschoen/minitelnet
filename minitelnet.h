#pragma once

#include <stddef.h>

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
	 * An option state is updated.
	 *
	 * If an option is being negotiated it can skip through different state
	 * as described in @ref telnet_option_state, most interesting ones to
	 * handle are the following:
	 *
	 * TELNET_OPTION_YES and TELNET_OPTION_NO declare an option as definitly
	 * enabled or disabled, other intermediate states should be considered
	 * "disabled" for the time being.
	 *
	 * TELNET_OPTION_REQUEST_PENDING is a non-standard option state which indicates
	 * an option is requested to be enabled and it is up to the application
	 * to accept or reject this request. A request is accepted or rejected using
	 * telnet_respond_negotiate(). It is not required to decide inside the
	 * callback, but a fast response is appriciated as negotiations could timeout
	 * or block the process.
	 */
	TELNET_EV_NEG,

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
	 * since the last mode switch. Summarizing, relaying on the offset as
	 * a absolute offset pointer is not recommended.
	 *
	 * The buffer is only valid for the duration of the callback.
	 */
	TELNET_EV_DATA,

	/**
	 * A malformed Telnet sequence was encountered.
	 *
	 * The specific error is available as event->error.
	 */
	TELNET_EV_ERROR
};

/** Errors detected while parsing the Telnet stream. */
enum telnet_error {
	/** SB was encountered while a subnegotiation was already active. */
	TELNET_ERR_INVALID_SB,

	/** SE was encountered while no subnegotiation was active. */
	TELNET_ERR_INVALID_SE,

	TELNET_ERR_NEGOTIATION,        /**< An invalid negotiation sequences is sent by peer. */
	TELNET_ERR_ALREADY_NEGOTIATING /**< telnet_send_negotiation() is called in middle of a ongoing negotiation */
};

/**
 * State of one direction of a Telnet option negotiation.
 *
 * The YES/NO/WANTYES/WANTNO states are defined in RFC 1143 as the Q-method
 * option negotiation. In this RFC the function and meaning for each state is
 * described more detailed.
 *
 * TELNET_OPTION_REQUEST_PENDING is a minitelnet extension. RFC 1143 assumes
 * that an unsolicited enable request is accepted or rejected when it is
 * processed. Minitelnet instead allows the application to make that policy
 * decision asynchronously through its event interface.
 */
enum telnet_option_state {
	/* RFC 1143 states */
	TELNET_OPTION_NO,               /**< Option is definitively disabled. (default) */
	TELNET_OPTION_YES,              /**< Option is definitively enabled. */
	TELNET_OPTION_WANTNO,           /**< Waiting for the option to become disabled. */
	TELNET_OPTION_WANTYES,          /**< Waiting for the option to become enabled. */
	TELNET_OPTION_WANTNO_OPPOSITE,  /**< Waiting for disable, but enable is now desired. */
	TELNET_OPTION_WANTYES_OPPOSITE, /**< Waiting for enable, but disable is now desired. */

	/* minitelnet extension */
	TELNET_OPTION_REQUEST_PENDING /**< Peer requested enabling the option; awaiting application decision. See TELNET_EV_NEG. */
};

/**
 * A contiguous chunk of streamed data.
 *
 * The pointed-to buffer is borrowed from the library or caller and must not
 * be retained after the event handler returns.
 */
struct telnet_event_data {
	const unsigned char *buffer; /**< First byte of this chunk. */
	size_t size;                 /**< Number of bytes in this chunk. */
	size_t offset;               /**< Stream-relative offset of the first byte. */
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
	const unsigned char *buffer; /**< Payload bytes in this chunk. */
	size_t size;                 /**< Number of payload bytes in this chunk. */
	size_t offset;               /**< Offset within the current subnegotiation. */
	unsigned char option;        /**< Option to which this subnegotiation belongs. */
};

/**
 * A change in option state.
 *
 * If this event is emitted, the state is already set to the new state.
 */
struct telnet_event_negotiate {
	unsigned char option;               /**< Telnet option number. */
	int local;                          /**< If option changed on the local or peer side. */
	enum telnet_option_state old_state; /**< State before change. */
	enum telnet_option_state new_state; /**< State after change. */
};

/**
 * Event payload passed to a telnet_handler_t.
 *
 * The active member is determined by enum telnet_event_type:
 *
 * - TELNET_EV_COMMAND: event.command
 * - TELNET_EV_SEND:    event.data
 * - TELNET_EV_NEG:     event.neg
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
 * The handler is invoked synchronously. Following events must be handled by
 * the application:
 *
 * TELNET_EV_SEND must be handled for the libray to be able to send replies and
 * send data.
 *
 * TELNET_EV_NEG with an condition on event->neg.new_state == TELNET_OPTION_REQUEST_PENDING.
 * By default the application can just reject every option request (which is boring..), which
 * is also the default behavious described in Telnet.
 *
 * TELNET_EV_DATA is not a hard dependency but usually a telnet connection is made
 * to send data back and forth.
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
	unsigned char _options[256];
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
 * Reset a Telnet session.
 *
 * Resets all protocol and option negotiation state to its initial state.
 * The configured event handler and user data are preserved.
 *
 * This can be used to reuse a Telnet instance for a new connection without
 * calling telnet_init() again.
 *
 * @param telnet Telnet instance.
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
 * Reminder that this library does not handle encoding states like Telnet BINARY aldus
 * if this option is not yet negotiated, line-endings and encoding must be handled
 * by the sender.
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
 * This explicit call is only required when you do intend to send the multiple
 * subnegotiations with the same option-code sequential.
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
 * If the application desires the peer end to enable an option, TELNET_CMD_DO
 * must be sent, to disable an option send TELNET_CMD_DONT. To mark an
 * option as enabled or disabled (or available/not available), TELNET_CMD_WILL
 * and TELNET_CMD_WONT must be called.
 *
 * Encoded negotiation bytes are emitted through TELNET_EV_SEND.
 */
void telnet_send_negotiate(struct telnet *telnet, enum telnet_command command, unsigned char option);

/**
 * Respond to a pending Telnet option negotiation request.
 *
 * @param telnet  Telnet state.
 * @param command One of TELNET_CMD_WILL, TELNET_CMD_WONT,
 *                TELNET_CMD_DO or TELNET_CMD_DONT.
 * @param option  Telnet option number.
 *
 * This function accepts or rejects an option enable request previously
 * received from the peer and reported as TELNET_OPTION_REQUEST_PENDING
 * through TELNET_EV_NEG.
 *
 * A received DO request is accepted with WILL and rejected with WONT.
 * A received WILL request is accepted with DO and rejected with DONT.
 *
 * If there is no matching pending request, this function has no effect.
 * In particular, it will never initiate a new negotiation. Use
 * telnet_send_negotiate() to initiate an option state change.
 *
 * Encoded negotiation bytes are emitted through TELNET_EV_SEND.
 */
void telnet_respond_negotiate(struct telnet *telnet, enum telnet_command command, unsigned char option);

/**
 * Return the negotiation state of a local option.
 *
 * A local option is an option performed by this endpoint. Its state is
 * negotiated by sending WILL/WONT or receiving DO/DONT.
 *
 * @param telnet Telnet instance.
 * @param option Telnet option number.
 *
 * @return Current negotiation state of the local option.
 */
enum telnet_option_state
telnet_option_local(const struct telnet *telnet, unsigned char option);

/**
 * Return the negotiation state of a peer option.
 *
 * A peer option is an option performed by the remote endpoint. Its state is
 * negotiated by sending DO/DONT or receiving WILL/WONT.
 *
 * @param telnet Telnet instance.
 * @param option Telnet option number.
 *
 * @return Current negotiation state of the peer option.
 */
enum telnet_option_state
telnet_option_peer(const struct telnet *telnet, unsigned char option);
