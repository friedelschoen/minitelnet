#include <assert.h>
#include <minitelnet.h>
#include <string.h>


void telnet_init(struct telnet *telnet, telnet_handler_t handler, void *userdata) {
	memset(telnet, 0, sizeof(*telnet));

	telnet->_handler = handler;
	telnet->_userdata = userdata;
	telnet->_state = TELNET_STATE_DATA;
	telnet->_write_sub_option = -1;
	telnet->sub_option = -1;
}

static void telnet_emit(struct telnet *telnet, enum telnet_event_type type, union telnet_event *event) {
	assert(telnet->_handler);

	telnet->_handler(telnet, type, event, telnet->_userdata);
}

static void telnet_send_raw(struct telnet *telnet, const void *data, size_t size) {
	union telnet_event ev;
	ev.data.buffer = data;
	ev.data.size = size;
	telnet_emit(telnet, TELNET_EV_SEND, &ev);
}

/* == NEGOTIATION LOGIC == */

static enum telnet_option_state telnet_option_transition(enum telnet_option_state current,
                                                         int outgoing,
                                                         int enable) {
	if (outgoing) {
		switch (current) {
			case TELNET_OPTION_DISABLED:
				return enable
				         ? TELNET_OPTION_WANT_ENABLED
				         : TELNET_OPTION_DISABLED;

			case TELNET_OPTION_ENABLED:
				return enable
				         ? TELNET_OPTION_ENABLED
				         : TELNET_OPTION_WANT_DISABLED;

			case TELNET_OPTION_THEY_WANT_ENABLED:
				return enable
				         ? TELNET_OPTION_ENABLED
				         : TELNET_OPTION_DISABLED;

			case TELNET_OPTION_THEY_WANT_DISABLED:
				return enable
				         ? TELNET_OPTION_ENABLED
				         : TELNET_OPTION_DISABLED;

			case TELNET_OPTION_WANT_ENABLED:
			case TELNET_OPTION_WANT_DISABLED:
				/* We already have an outstanding negotiation. */
				return current;
		}
	} else {
		switch (current) {
			case TELNET_OPTION_DISABLED:
				return enable
				         ? TELNET_OPTION_THEY_WANT_ENABLED
				         : TELNET_OPTION_DISABLED;

			case TELNET_OPTION_ENABLED:
				return enable
				         ? TELNET_OPTION_ENABLED
				         : TELNET_OPTION_THEY_WANT_DISABLED;

			case TELNET_OPTION_WANT_ENABLED:
				/* Response to our request to enable. */
				return enable
				         ? TELNET_OPTION_ENABLED
				         : TELNET_OPTION_DISABLED;

			case TELNET_OPTION_WANT_DISABLED:
				/* Response/conflict with our request to disable. */
				return enable
				         ? TELNET_OPTION_ENABLED
				         : TELNET_OPTION_DISABLED;

			case TELNET_OPTION_THEY_WANT_ENABLED:
			case TELNET_OPTION_THEY_WANT_DISABLED:
				/* Application hasn't answered the previous request yet. */
				return current;
		}
	}

	return current;
}

static uint8_t *telnet_option_state(struct telnet *telnet,
                                    enum telnet_command command,
                                    uint8_t option,
                                    int outgoing) {
	struct telnet_option *opt = &telnet->options[option];

	int will_side =
	    command == TELNET_CMD_WILL ||
	    command == TELNET_CMD_WONT;

	/*
	 * Outgoing:
	 *   WILL/WONT -> us
	 *   DO/DONT   -> them
	 *
	 * Incoming:
	 *   WILL/WONT -> them
	 *   DO/DONT   -> us
	 */
	if (outgoing)
		return will_side ? &opt->us : &opt->them;
	else
		return will_side ? &opt->them : &opt->us;
}

static enum telnet_option_state telnet_negotiate_transition(struct telnet *telnet,
                                                            enum telnet_command command,
                                                            uint8_t option,
                                                            int outgoing) {
	uint8_t *state =
	    telnet_option_state(telnet, command, option, outgoing);

	int enable =
	    command == TELNET_CMD_WILL ||
	    command == TELNET_CMD_DO;

	enum telnet_option_state old = *state;

	*state = telnet_option_transition(old, outgoing, enable);

	return old;
}

static void telnet_send_escaped(struct telnet *telnet, const void *data, size_t size) {
	const uint8_t *buffer = data;
	size_t start = 0;

	for (size_t i = 0; i < size; i++) {
		if (buffer[i] != TELNET_IAC)
			continue;

		if (i > start)
			telnet_send_raw(telnet, buffer + start, i - start);

		telnet_send_command(telnet, TELNET_IAC);
		start = i + 1;
	}

	if (start < size)
		telnet_send_raw(telnet, buffer + start, size - start);
}

void telnet_send_data(struct telnet *telnet, const void *data, size_t size) {
	const uint8_t *buffer = data;
	size_t start = 0;

	if (telnet->_write_sub_option != -1) {
		telnet->_write_sub_option = -1;
		telnet_send_command(telnet, TELNET_CMD_SE);
	}

	telnet_send_escaped(telnet, data, size);
}

void telnet_send_subnegotiation(struct telnet *telnet, uint8_t option, const void *data, size_t size) {
	if (telnet->_write_sub_option != option) {
		if (telnet->_write_sub_option != -1)
			/* if currently writing to a different subnegotiation, end that */
			telnet_send_command(telnet, TELNET_CMD_SE);

		telnet->_write_sub_option = option;
		telnet_send_command(telnet, TELNET_CMD_SB);
		telnet_send_raw(telnet, &option, 1);
	}

	telnet_send_escaped(telnet, data, size);
}

void telnet_send_command(struct telnet *telnet, enum telnet_command command) {
	uint8_t out[2];

	out[0] = TELNET_IAC;
	out[1] = command;
	telnet_send_raw(telnet, out, 2);
}

void telnet_send_negotiate(struct telnet *telnet, enum telnet_command command, uint8_t option) {
	uint8_t out[3];

	telnet_negotiate_transition(
	    telnet, command, option, 1);

	out[0] = TELNET_IAC;
	out[1] = command;
	out[2] = option;

	telnet_send_raw(telnet, out, sizeof(out));
}


static void telnet_handle_command(struct telnet *telnet, enum telnet_command cmd) {
	union telnet_event ev;
	switch (cmd) {
		case TELNET_CMD_SE:
			if (telnet->sub_option == -1) {
				ev.error = TELNET_ERROR_INVALID_SE;
				telnet_emit(telnet, TELNET_EV_ERROR, &ev);
			}
			telnet->sub_option = -1;
			telnet->_state = TELNET_STATE_DATA;
			break;

		case TELNET_CMD_NOP:
		case TELNET_CMD_DM:
		case TELNET_CMD_BRK:
		case TELNET_CMD_IP:
		case TELNET_CMD_AO:
		case TELNET_CMD_AYT:
		case TELNET_CMD_EC:
		case TELNET_CMD_EL:
		case TELNET_CMD_GA:
			ev.command = cmd;
			telnet_emit(telnet, TELNET_EV_COMMAND, &ev);
			telnet->_state = TELNET_STATE_DATA;
			break;

		case TELNET_CMD_SB:
			if (telnet->sub_option != -1) {
				ev.error = TELNET_ERROR_INVALID_SB;
				telnet_emit(telnet, TELNET_EV_ERROR, &ev);
			}
			telnet->_state = TELNET_STATE_SUBNEG_OPTION;
			break;

		case TELNET_CMD_WILL:
		case TELNET_CMD_WONT:
		case TELNET_CMD_DO:
		case TELNET_CMD_DONT:
			telnet->_command = cmd;
			telnet->_state = TELNET_STATE_OPTION;
			break;

		case TELNET_IAC:
			ev.data.buffer = &cmd; /* escape */
			ev.data.size = 1;
			telnet_emit(telnet, TELNET_EV_DATA, &ev);
			telnet->_state = TELNET_STATE_DATA;
			break;
	}
}


static void telnet_handle_negotiation(struct telnet *telnet, uint8_t option) {
	union telnet_event ev;
	enum telnet_command command = telnet->_command;

	enum telnet_option_state old =
	    telnet_negotiate_transition(
	        telnet, command, option, 0);

	ev.neg.command = command;
	ev.neg.option = option;

	/*
	 * WANT_* means this incoming negotiation is a response
	 * to a negotiation we initiated.
	 *
	 * Otherwise it originated at the peer.
	 */
	if (old == TELNET_OPTION_WANT_ENABLED ||
	    old == TELNET_OPTION_WANT_DISABLED)
		telnet_emit(telnet, TELNET_EV_NEG_US, &ev);
	else
		telnet_emit(telnet, TELNET_EV_NEG_THEM, &ev);

	telnet->_state = TELNET_STATE_DATA;
}

static void telnet_feed_char(struct telnet *telnet, uint8_t chr) {
	switch (telnet->_state) {
		case TELNET_STATE_DATA:
			/* oops, that should not happen */
			return;

		case TELNET_STATE_COMMAND:
			telnet_handle_command(telnet, (enum telnet_command) chr);
			break;

		case TELNET_STATE_OPTION:
			telnet_handle_negotiation(telnet, chr);
			break;

		case TELNET_STATE_SUBNEG_OPTION:
			telnet->sub_option = chr;
			telnet->_state = TELNET_STATE_DATA;
			break;
	}
}

void telnet_feed(struct telnet *telnet, const void *data, size_t size) {
	const uint8_t *buffer = data;
	union telnet_event ev;
	size_t i = 0;

	while (i < size) {
		if (telnet->_state == TELNET_STATE_DATA) {
			size_t start = i;

			while (i < size && buffer[i] != TELNET_IAC)
				i++;

			if (i > start) {
				ev.data.buffer = buffer + start,
				ev.data.size = i - start,
				telnet_emit(telnet, TELNET_EV_DATA, &ev);
			}

			if (i < size) {
				telnet->_state = TELNET_STATE_COMMAND;
				i++; /* consume IAC */
			}
		} else {
			telnet_feed_char(telnet, buffer[i++]);
		}
	}
}
