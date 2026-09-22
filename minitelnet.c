/* SPDX-License-Identifier: Zlib */
/* Copyright (c) 2026 Friedel Schön */

#include <assert.h>
#include <minitelnet.h>
#include <string.h>

enum telnet_state {
	TELNET_STATE_DATA,         /* currently receiving data */
	TELNET_STATE_COMMAND,      /* just received an IAC and expect a command */
	TELNET_STATE_OPTION,       /* received an WILL/WONT/DO/DONT command; expecting an option */
	TELNET_STATE_SUBNEG_OPTION /* expecting subnegotiation option */
};

void telnet_init(struct telnet *telnet, telnet_handler_t handler, void *userdata) {
	memset(telnet, 0, sizeof(*telnet));

	telnet->_handler = handler;
	telnet->_userdata = userdata;
	telnet->_state = TELNET_STATE_DATA;
	telnet->_send_sub_option = -1;
	telnet->_recv_sub_option = -1;
}

void telnet_reset(struct telnet *telnet) {
	telnet_handler_t handler = telnet->_handler;
	void *userdata = telnet->_userdata;

	telnet_init(telnet, handler, userdata);
}

static void telnet_emit(struct telnet *telnet, enum telnet_event_type type, union telnet_event *event) {
	assert(telnet->_handler); /* using assert as handler must never be null */

	telnet->_handler(telnet, type, event, telnet->_userdata);
}

static void telnet_send_raw(struct telnet *telnet, const unsigned char *data, size_t size) {
	union telnet_event ev;
	ev.data.buffer = data;
	ev.data.size = size;
	telnet_emit(telnet, TELNET_EV_SEND, &ev);
}

static void telnet_write_raw(struct telnet *telnet, const unsigned char *data, size_t size) {
	union telnet_event ev;
	ev.data.buffer = data;
	ev.data.size = size;
	ev.data.offset = telnet->_recv_offset;
	telnet->_recv_offset += size;

	if (telnet->_recv_sub_option != -1) {
		ev.subneg.option = telnet->_recv_sub_option;
		/* &ev->data == &ev->subneg */
		telnet_emit(telnet, TELNET_EV_SUBNEG, &ev);
	} else {
		telnet_emit(telnet, TELNET_EV_DATA, &ev);
	}
}

/* == NEGOTIATION LOGIC == */

enum telnet_option_state telnet_option_local(const struct telnet *telnet, unsigned char option) {
	return (enum telnet_option_state)(telnet->_options[option] & 0x0f);
}

enum telnet_option_state telnet_option_peer(const struct telnet *telnet, unsigned char option) {
	return (enum telnet_option_state)((telnet->_options[option] & 0xf0) >> 4);
}

static void telnet_set_option_local(struct telnet *telnet, unsigned char option, enum telnet_option_state state) {
	union telnet_event event;
	unsigned char old = telnet->_options[option] & 0x0f;

	if (old == state)
		return;

	telnet->_options[option] &= 0xf0;
	telnet->_options[option] |= (unsigned char) state;

	event.neg.option = option;
	event.neg.local = 1;
	event.neg.old_state = old;
	event.neg.new_state = state;
	telnet_emit(telnet, TELNET_EV_NEG, &event);
}

static void telnet_set_option_peer(struct telnet *telnet, unsigned char option, enum telnet_option_state state) {
	union telnet_event event;
	unsigned char old = telnet->_options[option] >> 4;

	if (old == state)
		return;

	telnet->_options[option] &= 0x0f;
	telnet->_options[option] |= (unsigned char) state << 4;

	event.neg.option = option;
	event.neg.local = 0;
	event.neg.old_state = old;
	event.neg.new_state = state;
	telnet_emit(telnet, TELNET_EV_NEG, &event);
}

static void telnet_send_negotiation_raw(struct telnet *telnet, enum telnet_command cmd, unsigned char option) {
	unsigned char out[3];
	out[0] = TELNET_IAC;
	out[1] = cmd;
	out[2] = option;
	telnet_send_raw(telnet, out, sizeof(out));
}

static void telnet_handle_rfc1143(struct telnet *telnet, enum telnet_command cmd, unsigned char option) {
	union telnet_event event;
	enum telnet_option_state local = telnet_option_local(telnet, option),
	                         peer = telnet_option_peer(telnet, option);

	if (cmd == TELNET_CMD_WILL) {
		/* == page 7 ==
		Upon receipt of WILL, we choose based upon him and himq:
		  NO            If we agree that he should enable, him=YES, send
		                DO; otherwise, send DONT.
		  YES           Ignore.
		  WANTNO  EMPTY Error: DONT answered by WILL. him=NO.
		       OPPOSITE Error: DONT answered by WILL. him=YES*,
		                himq=EMPTY.
		  WANTYES EMPTY him=YES.
		       OPPOSITE him=WANTNO, himq=EMPTY, send DONT.
		 */
		switch (peer) {
			case TELNET_OPTION_YES:
				/* ignore */
				break;
			case TELNET_OPTION_NO:
				telnet_set_option_peer(telnet, option, TELNET_OPTION_REQUEST_PENDING);
				break;
			case TELNET_OPTION_WANTYES:
				telnet_set_option_peer(telnet, option, TELNET_OPTION_YES);
				break;
			case TELNET_OPTION_WANTNO:
				event.error = TELNET_ERR_NEGOTIATION;
				telnet_emit(telnet, TELNET_EV_ERROR, &event);
				telnet_set_option_peer(telnet, option, TELNET_OPTION_NO);
				break;
			case TELNET_OPTION_WANTYES_OPPOSITE:
				telnet_send_negotiation_raw(telnet, TELNET_CMD_DONT, option);
				telnet_set_option_peer(telnet, option, TELNET_OPTION_WANTNO);
				break;
			case TELNET_OPTION_WANTNO_OPPOSITE:
				event.error = TELNET_ERR_NEGOTIATION;
				telnet_emit(telnet, TELNET_EV_ERROR, &event);
				telnet_set_option_peer(telnet, option, TELNET_OPTION_YES);
				break;
			case TELNET_OPTION_REQUEST_PENDING:
				/* already requested */
				break;
		}
	} else if (cmd == TELNET_CMD_WONT) {
		/* == page 8 ==
		Upon receipt of WONT, we choose based upon him and himq:
		  NO            Ignore.
		  YES           him=NO, send DONT.
		  WANTNO  EMPTY him=NO.
		       OPPOSITE him=WANTYES, himq=NONE, send DO.
		  WANTYES EMPTY him=NO.*
		       OPPOSITE him=NO, himq=NONE.**
		 */
		switch (peer) {
			case TELNET_OPTION_YES:
				telnet_send_negotiation_raw(telnet, TELNET_CMD_DONT, option);
				telnet_set_option_peer(telnet, option, TELNET_OPTION_NO);
				break;
			case TELNET_OPTION_NO:
				/* ignore */
				break;
			case TELNET_OPTION_WANTYES:
				telnet_set_option_peer(telnet, option, TELNET_OPTION_NO);
				break;
			case TELNET_OPTION_WANTNO:
				telnet_set_option_peer(telnet, option, TELNET_OPTION_NO);
				break;
			case TELNET_OPTION_WANTYES_OPPOSITE:
				telnet_set_option_peer(telnet, option, TELNET_OPTION_NO);
				break;
			case TELNET_OPTION_WANTNO_OPPOSITE:
				telnet_send_negotiation_raw(telnet, TELNET_CMD_DO, option);
				telnet_set_option_peer(telnet, option, TELNET_OPTION_WANTYES);
				break;
			case TELNET_OPTION_REQUEST_PENDING:
				telnet_set_option_peer(telnet, option, TELNET_OPTION_NO);
				break;
		}
	} else if (cmd == TELNET_CMD_DO) {
		/* == page 7 ==
		Upon receipt of DO, we choose based upon him and himq:
		  NO            If we agree that he should enable, him=YES, send
		                WILL; otherwise, send WONT.
		  YES           Ignore.
		  WANTNO  EMPTY Error: WONT answered by DO. him=NO.
		       OPPOSITE Error: WONT answered by DO. him=YES*,
		                himq=EMPTY.
		  WANTYES EMPTY him=YES.
		       OPPOSITE him=WANTNO, himq=EMPTY, send WONT.
		 */
		switch (local) {
			case TELNET_OPTION_YES:
				/* ignore */
				break;
			case TELNET_OPTION_NO:
				telnet_set_option_local(telnet, option, TELNET_OPTION_REQUEST_PENDING);
				break;
			case TELNET_OPTION_WANTYES:
				telnet_set_option_local(telnet, option, TELNET_OPTION_YES);
				break;
			case TELNET_OPTION_WANTNO:
				event.error = TELNET_ERR_NEGOTIATION;
				telnet_emit(telnet, TELNET_EV_ERROR, &event);
				telnet_set_option_local(telnet, option, TELNET_OPTION_NO);
				break;
			case TELNET_OPTION_WANTYES_OPPOSITE:
				telnet_send_negotiation_raw(telnet, TELNET_CMD_WONT, option);
				telnet_set_option_local(telnet, option, TELNET_OPTION_WANTNO);
				break;
			case TELNET_OPTION_WANTNO_OPPOSITE:
				event.error = TELNET_ERR_NEGOTIATION;
				telnet_emit(telnet, TELNET_EV_ERROR, &event);
				telnet_set_option_local(telnet, option, TELNET_OPTION_YES);
				break;
			case TELNET_OPTION_REQUEST_PENDING:
				/* already requested */
				break;
		}
	} else if (cmd == TELNET_CMD_DONT) {
		/* == page 8 ==
		Upon receipt of DONT, we choose based upon him and himq:
		  NO            Ignore.
		  YES           him=NO, send WONT.
		  WANTNO  EMPTY him=NO.
		       OPPOSITE him=WANTYES, himq=NONE, send WILL.
		  WANTYES EMPTY him=NO.*
		       OPPOSITE him=NO, himq=NONE.**
		 */
		switch (local) {
			case TELNET_OPTION_YES:
				telnet_send_negotiation_raw(telnet, TELNET_CMD_WONT, option);
				telnet_set_option_local(telnet, option, TELNET_OPTION_NO);
				break;
			case TELNET_OPTION_NO:
				/* ignore */
				break;
			case TELNET_OPTION_WANTYES:
				telnet_set_option_local(telnet, option, TELNET_OPTION_NO);
				break;
			case TELNET_OPTION_WANTNO:
				telnet_set_option_local(telnet, option, TELNET_OPTION_NO);
				break;
			case TELNET_OPTION_WANTYES_OPPOSITE:
				telnet_set_option_local(telnet, option, TELNET_OPTION_NO);
				break;
			case TELNET_OPTION_WANTNO_OPPOSITE:
				telnet_send_negotiation_raw(telnet, TELNET_CMD_WILL, option);
				telnet_set_option_local(telnet, option, TELNET_OPTION_WANTYES);
				break;
			case TELNET_OPTION_REQUEST_PENDING:
				telnet_set_option_local(telnet, option, TELNET_OPTION_NO);
				break;
		}
	}
}

void telnet_respond_negotiation(struct telnet *telnet, enum telnet_command command, unsigned char option) {
	enum telnet_option_state local = telnet_option_local(telnet, option),
	                         peer = telnet_option_peer(telnet, option);

	switch (command) {
		case TELNET_CMD_WILL:
		case TELNET_CMD_WONT:
			if (local != TELNET_OPTION_REQUEST_PENDING)
				return;

			telnet_send_negotiation_raw(telnet, command, option);
			telnet_set_option_local(telnet, option, command == TELNET_CMD_WILL ? TELNET_OPTION_YES : TELNET_OPTION_NO);
			break;

		case TELNET_CMD_DO:
		case TELNET_CMD_DONT:
			if (peer != TELNET_OPTION_REQUEST_PENDING)
				return;

			telnet_send_negotiation_raw(telnet, command, option);
			telnet_set_option_peer(telnet, option, command == TELNET_CMD_DO ? TELNET_OPTION_YES : TELNET_OPTION_NO);
			break;
		default:
			return;
	}
}

void telnet_send_negotiation(struct telnet *telnet, enum telnet_command command, unsigned char option) {
	union telnet_event event;
	enum telnet_option_state local = telnet_option_local(telnet, option),
	                         peer = telnet_option_peer(telnet, option);

	if (command == TELNET_CMD_DO) {
		/*
		If we decide to ask him to enable:
		  NO            him=WANTYES, send DO.
		  YES           Error: Already enabled.
		  WANTNO  EMPTY If we are queueing requests, himq=OPPOSITE;
		                otherwise, Error: Cannot initiate new request
		                in the middle of negotiation.
		       OPPOSITE Error: Already queued an enable request.
		  WANTYES EMPTY Error: Already negotiating for enable.
		       OPPOSITE himq=EMPTY.
		*/
		switch (peer) {
			case TELNET_OPTION_NO:
				telnet_send_negotiation_raw(telnet, TELNET_CMD_DO, option);
				telnet_set_option_peer(telnet, option, TELNET_OPTION_WANTYES);
				break;
			case TELNET_OPTION_YES:
				/* already enabled */
				break;
			case TELNET_OPTION_WANTNO:
				telnet_set_option_peer(telnet, option, TELNET_OPTION_WANTNO_OPPOSITE);
				break;
			case TELNET_OPTION_WANTYES_OPPOSITE:
				telnet_set_option_peer(telnet, option, TELNET_OPTION_WANTYES);
				break;
			case TELNET_OPTION_WANTYES:
			case TELNET_OPTION_WANTNO_OPPOSITE:
				event.error = TELNET_ERR_ALREADY_NEGOTIATING;
				telnet_emit(telnet, TELNET_EV_ERROR, &event);
				break;
			case TELNET_OPTION_REQUEST_PENDING:
				break;
		}
	} else if (command == TELNET_CMD_DONT) {
		/*
		If we decide to ask him to disable:
		  NO            Error: Already disabled.
		  YES           him=WANTNO, send DONT.
		  WANTNO  EMPTY Error: Already negotiating for disable.
		       OPPOSITE himq=EMPTY.
		  WANTYES EMPTY If we are queueing requests, himq=OPPOSITE;
		                otherwise, Error: Cannot initiate new request
		                in the middle of negotiation.
		       OPPOSITE Error: Already queued a disable request.
		*/
		switch (peer) {
			case TELNET_OPTION_NO:
				/* already disabled */
				break;
			case TELNET_OPTION_YES:
				telnet_send_negotiation_raw(telnet, TELNET_CMD_DONT, option);
				telnet_set_option_peer(telnet, option, TELNET_OPTION_WANTNO);
				break;
			case TELNET_OPTION_WANTYES:
				telnet_set_option_peer(telnet, option, TELNET_OPTION_WANTYES_OPPOSITE);
				break;
			case TELNET_OPTION_WANTNO_OPPOSITE:
				telnet_set_option_peer(telnet, option, TELNET_OPTION_WANTNO);
				break;
			case TELNET_OPTION_WANTNO:
			case TELNET_OPTION_WANTYES_OPPOSITE:
				event.error = TELNET_ERR_ALREADY_NEGOTIATING;
				telnet_emit(telnet, TELNET_EV_ERROR, &event);
				break;
			case TELNET_OPTION_REQUEST_PENDING:
				break;
		}
	} else if (command == TELNET_CMD_WILL) {
		/*
		If we decide to ask him to enable:
		  NO            him=WANTYES, send DO.
		  YES           Error: Already enabled.
		  WANTNO  EMPTY If we are queueing requests, himq=OPPOSITE;
		                otherwise, Error: Cannot initiate new request
		                in the middle of negotiation.
		       OPPOSITE Error: Already queued an enable request.
		  WANTYES EMPTY Error: Already negotiating for enable.
		       OPPOSITE himq=EMPTY.
		*/
		switch (local) {
			case TELNET_OPTION_NO:
				telnet_send_negotiation_raw(telnet, TELNET_CMD_WILL, option);
				telnet_set_option_local(telnet, option, TELNET_OPTION_WANTYES);
				break;
			case TELNET_OPTION_YES:
				/* already enabled */
				break;
			case TELNET_OPTION_WANTNO:
				telnet_set_option_local(telnet, option, TELNET_OPTION_WANTNO_OPPOSITE);
				break;
			case TELNET_OPTION_WANTYES_OPPOSITE:
				telnet_set_option_local(telnet, option, TELNET_OPTION_WANTYES);
				break;
			case TELNET_OPTION_WANTYES:
			case TELNET_OPTION_WANTNO_OPPOSITE:
				event.error = TELNET_ERR_ALREADY_NEGOTIATING;
				telnet_emit(telnet, TELNET_EV_ERROR, &event);
				break;
			case TELNET_OPTION_REQUEST_PENDING:
				break;
		}
	} else if (command == TELNET_CMD_WONT) {
		/*
		If we decide to ask him to disable:
		  NO            Error: Already disabled.
		  YES           him=WANTNO, send DONT.
		  WANTNO  EMPTY Error: Already negotiating for disable.
		       OPPOSITE himq=EMPTY.
		  WANTYES EMPTY If we are queueing requests, himq=OPPOSITE;
		                otherwise, Error: Cannot initiate new request
		                in the middle of negotiation.
		       OPPOSITE Error: Already queued a disable request.
		*/
		switch (local) {
			case TELNET_OPTION_NO:
				/* already disabled */
				break;
			case TELNET_OPTION_YES:
				telnet_send_negotiation_raw(telnet, TELNET_CMD_WONT, option);
				telnet_set_option_local(telnet, option, TELNET_OPTION_WANTNO);
				break;
			case TELNET_OPTION_WANTYES:
				telnet_set_option_local(telnet, option, TELNET_OPTION_WANTYES_OPPOSITE);
				break;
			case TELNET_OPTION_WANTNO_OPPOSITE:
				telnet_set_option_local(telnet, option, TELNET_OPTION_WANTNO);
				break;
			case TELNET_OPTION_WANTNO:
			case TELNET_OPTION_WANTYES_OPPOSITE:
				event.error = TELNET_ERR_ALREADY_NEGOTIATING;
				telnet_emit(telnet, TELNET_EV_ERROR, &event);
				break;
			case TELNET_OPTION_REQUEST_PENDING:
				break;
		}
	}
}

static void telnet_send_escaped(struct telnet *telnet, const unsigned char *data, size_t size) {
	size_t start = 0, i;

	for (i = 0; i < size; i++) {
		if (data[i] != TELNET_IAC)
			continue;

		if (i > start)
			telnet_send_raw(telnet, data + start, i - start);

		telnet_send_command(telnet, TELNET_CMD_ESC);
		start = i + 1;
	}

	if (start < size)
		telnet_send_raw(telnet, data + start, size - start);
}

void telnet_send_data(struct telnet *telnet, const unsigned char *data, size_t size) {
	if (telnet->_send_sub_option != -1) {
		telnet->_send_sub_option = -1;
		telnet_send_command(telnet, TELNET_CMD_SE);
	}

	telnet_send_escaped(telnet, data, size);
}

void telnet_send_subnegotiation(struct telnet *telnet, unsigned char option, const unsigned char *data, size_t size) {
	if (telnet->_send_sub_option != option) {
		if (telnet->_send_sub_option != -1)
			/* if currently writing to a different subnegotiation, end that */
			telnet_send_command(telnet, TELNET_CMD_SE);

		telnet->_send_sub_option = option;
		telnet_send_command(telnet, TELNET_CMD_SB);
		telnet_send_raw(telnet, &option, 1);
	}

	telnet_send_escaped(telnet, data, size);
}

void telnet_send_subnegotiation_end(struct telnet *telnet, unsigned char option) {
	if (telnet->_send_sub_option != option)
		/* already ended */
		return;

	telnet->_send_sub_option = -1;
	telnet_send_command(telnet, TELNET_CMD_SE);
}

void telnet_send_command(struct telnet *telnet, enum telnet_command command) {
	unsigned char out[2];

	out[0] = TELNET_IAC;
	out[1] = command;
	telnet_send_raw(telnet, out, 2);
}

static void telnet_handle_command(struct telnet *telnet, enum telnet_command cmd) {
	union telnet_event ev;
	switch (cmd) {
		case TELNET_CMD_SE:
			if (telnet->_recv_sub_option == -1) {
				ev.error = TELNET_ERR_INVALID_SE;
				telnet_emit(telnet, TELNET_EV_ERROR, &ev);
			}
			telnet->_recv_sub_option = -1;
			telnet->_recv_offset = 0;
			telnet->_state = TELNET_STATE_DATA;
			break;

		case TELNET_CMD_SB:
			if (telnet->_recv_sub_option != -1) {
				ev.error = TELNET_ERR_INVALID_SB;
				telnet_emit(telnet, TELNET_EV_ERROR, &ev);
			}
			telnet->_recv_offset = 0;
			telnet->_state = TELNET_STATE_SUBNEG_OPTION;
			break;

		case TELNET_CMD_WILL:
		case TELNET_CMD_WONT:
		case TELNET_CMD_DO:
		case TELNET_CMD_DONT:
			telnet->_command = cmd;
			telnet->_state = TELNET_STATE_OPTION;
			break;

		case TELNET_CMD_ESC:
			telnet_write_raw(telnet, (unsigned char *) &cmd, 1);
			telnet->_state = TELNET_STATE_DATA;
			break;

		default:
			ev.command = cmd;
			telnet_emit(telnet, TELNET_EV_COMMAND, &ev);
			telnet->_state = TELNET_STATE_DATA;
			break;
	}
}

static void telnet_handle_negotiation(struct telnet *telnet, unsigned char option) {
	telnet_handle_rfc1143(telnet, telnet->_command, option);
	telnet->_state = TELNET_STATE_DATA;
}

static void telnet_feed_char(struct telnet *telnet, unsigned char chr) {
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
			telnet->_recv_sub_option = chr;
			telnet->_recv_offset = 0;
			telnet->_state = TELNET_STATE_DATA;
			break;
	}
}

void telnet_feed(struct telnet *telnet, const unsigned char *data, size_t size) {
	size_t i = 0;

	while (i < size) {
		if (telnet->_state == TELNET_STATE_DATA) {
			size_t start = i;

			while (i < size && data[i] != TELNET_IAC)
				i++;

			if (i > start) {
				telnet_write_raw(telnet, data + start, i - start);
			}

			if (i < size) {
				telnet->_state = TELNET_STATE_COMMAND;
				i++; /* consume IAC */
			}
		} else {
			telnet_feed_char(telnet, data[i++]);
		}
	}
}
