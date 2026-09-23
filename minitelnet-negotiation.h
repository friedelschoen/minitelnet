#pragma once

#include <minitelnet.h>
#include <stddef.h>

/**
 * @defgroup negotiation RFC 1143 Option Negotiation
 * @{
 */

/**
 * State of one direction of a Telnet option negotiation.
 *
 * Each Telnet option has two independent negotiation states: one for the
 * local endpoint and one for the peer. The NO, YES, WANTNO and WANTYES
 * states, including their OPPOSITE variants, implement the Q method defined
 * by RFC 1143.
 *
 * TELNET_OPTION_REQUEST_PENDING is a minitelnet extension. It represents an
 * unsolicited request from the peer to enable an option for which the
 * application has not yet made a policy decision. The application may later
 * accept or reject the request using telnet_negotiation_respond().
 */
enum telnet_negotiation_state {
	/* RFC 1143 states */
	TELNET_OPTION_NO,               /**< Option is disabled. Initial state. */
	TELNET_OPTION_YES,              /**< Option is enabled. */
	TELNET_OPTION_WANTNO,           /**< Waiting for the option to become disabled. */
	TELNET_OPTION_WANTYES,          /**< Waiting for the option to become enabled. */
	TELNET_OPTION_WANTNO_OPPOSITE,  /**< Waiting for disable; enable is queued. */
	TELNET_OPTION_WANTYES_OPPOSITE, /**< Waiting for enable; disable is queued. */

	/* minitelnet extension */
	TELNET_OPTION_REQUEST_PENDING /**< Peer requested enable; awaiting application decision. */

	/* Keep below 16: states are stored in 4-bit nibbles. */
};

/**
 * Result of an option state transition.
 *
 * A transition is produced whenever one direction of an option changes
 * state. The option, direction and resulting state identify the transition.
 *
 * Some transitions require a WILL, WONT, DO or DONT command to be sent to
 * the peer. In that case outgoing contains the required command; otherwise
 * it is zero. The command can be encoded and emitted using
 * telnet_send_negotiation().
 *
 * error is non-zero when the transition was caused by an inconsistent reply
 * from the peer, such as receiving WILL while waiting for a reply to DONT.
 * The state machine still performs the recovery transition prescribed by
 * RFC 1143.
 */
struct telnet_negotiation_transition {
	unsigned char option;                /**< Option whose state changed. */
	int local;                           /**< Non-zero for local state; zero for peer state. */
	enum telnet_negotiation_state state; /**< State after the transition. */

	int error;                    /**< Non-zero if the peer sent an inconsistent reply. */
	enum telnet_command outgoing; /**< Command to send to the peer, or zero if none. */
};

/**
 * RFC 1143 negotiation state for all Telnet options.
 *
 * Each option occupies one byte. The lower nibble contains the local state
 * and the upper nibble contains the peer state. A zero-initialized object is
 * therefore a valid initial state in which every option is disabled.
 *
 * The negotiation state is independent of struct telnet and performs no
 * transport I/O or Telnet wire encoding.
 */
typedef unsigned char telnet_negotiation_t[256];

/**
 * Process a negotiation command received from the peer.
 *
 * @param neg    Negotiation state.
 * @param cmd    Received TELNET_CMD_WILL, TELNET_CMD_WONT,
 *               TELNET_CMD_DO or TELNET_CMD_DONT.
 * @param option Telnet option number accompanying the command.
 * @param trns   Receives the resulting transition when the state changes.
 *
 * This function applies an incoming WILL, WONT, DO or DONT command to the
 * RFC 1143 state machine. An unsolicited request to enable an option enters
 * TELNET_OPTION_REQUEST_PENDING so that the application can decide whether
 * to accept or reject it with telnet_negotiation_respond().
 *
 * If the transition requires a reply, trns->outgoing contains the command
 * that must be sent using telnet_send_negotiation().
 *
 * @return Non-zero if an option state changed and @p trns was populated;
 *         zero if the command caused no state transition.
 */
int telnet_negotiation_feed(telnet_negotiation_t neg, enum telnet_command cmd,
                            unsigned char option, struct telnet_negotiation_transition *trns);

/**
 * Request a change to an option's negotiated state.
 *
 * @param neg     Negotiation state.
 * @param command Desired TELNET_CMD_WILL, TELNET_CMD_WONT,
 *                TELNET_CMD_DO or TELNET_CMD_DONT.
 * @param option  Telnet option number.
 * @param trns    Receives the resulting transition when the state changes.
 *
 * WILL and WONT express the desired state of a local option. DO and DONT
 * express the desired state of the corresponding peer option.
 *
 * The Q method handles requests made while another negotiation is in
 * progress by queueing or cancelling the opposite desired state as
 * appropriate. Requests which do not change the current or queued desired
 * state have no effect.
 *
 * If the transition requires a command to be transmitted,
 * trns->outgoing contains that command. The caller should pass it to
 * telnet_send_negotiation().
 *
 * This function does not resolve TELNET_OPTION_REQUEST_PENDING. Use
 * telnet_negotiation_respond() to accept or reject a pending peer request.
 *
 * @return Non-zero if an option state changed and @p trns was populated;
 *         zero if the request caused no state transition.
 */
int telnet_negotiation_send(telnet_negotiation_t neg, enum telnet_command command,
                            unsigned char option, struct telnet_negotiation_transition *trns);

/**
 * Respond to a pending option enable request from the peer.
 *
 * @param neg     Negotiation state.
 * @param command Response command: TELNET_CMD_WILL, TELNET_CMD_WONT,
 *                TELNET_CMD_DO or TELNET_CMD_DONT.
 * @param option  Telnet option number.
 * @param trns    Receives the resulting transition when the state changes.
 *
 * A pending local option request, originating from a received DO, is
 * accepted with WILL and rejected with WONT. A pending peer option request,
 * originating from a received WILL, is accepted with DO and rejected with
 * DONT.
 *
 * The response is only applied when the corresponding direction is in
 * TELNET_OPTION_REQUEST_PENDING. Otherwise the call has no effect and never
 * initiates a new negotiation.
 *
 * A successful response always places the option in YES or NO and sets
 * trns->outgoing to the supplied response command. The caller should pass
 * that command to telnet_send_negotiation().
 *
 * @return Non-zero if a pending request was resolved and @p trns was
 *         populated; zero if there was no matching pending request.
 */
int telnet_negotiation_respond(telnet_negotiation_t neg, enum telnet_command command,
                               unsigned char option, struct telnet_negotiation_transition *trns);

/**
 * Return the local negotiation state of an option.
 *
 * A local option describes functionality performed by this endpoint. Its
 * state is affected by locally sending WILL/WONT and by receiving DO/DONT
 * from the peer.
 *
 * @param neg    Negotiation state.
 * @param option Telnet option number.
 *
 * @return Current local negotiation state.
 */
enum telnet_negotiation_state
telnet_negotiation_local(const telnet_negotiation_t neg, unsigned char option);

/**
 * Return the peer negotiation state of an option.
 *
 * A peer option describes functionality performed by the remote endpoint.
 * Its state is affected by locally sending DO/DONT and by receiving
 * WILL/WONT from the peer.
 *
 * @param neg    Negotiation state.
 * @param option Telnet option number.
 *
 * @return Current peer negotiation state.
 */
enum telnet_negotiation_state
telnet_negotiation_peer(const telnet_negotiation_t neg, unsigned char option);

/** @} */
