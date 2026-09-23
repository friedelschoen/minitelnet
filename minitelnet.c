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

static void telnet_emit(struct telnet *telnet, union telnet_event *event) {
	assert(telnet->_handler); /* using assert as handler must never be null */

	telnet->_handler(telnet, event, telnet->_userdata);
}

static void telnet_send_raw(struct telnet *telnet, const unsigned char *data, size_t size) {
	union telnet_event ev;
	ev.type = TELNET_EV_SEND;
	ev.data.buffer = data;
	ev.data.size = size;
	telnet_emit(telnet, &ev);
}

static void telnet_write_raw(struct telnet *telnet, const unsigned char *data, size_t size) {
	union telnet_event ev;
	ev.data.buffer = data;
	ev.data.size = size;

	if (telnet->_recv_sub_option != -1) {
		ev.type = TELNET_EV_SUBNEG;
		ev.subneg.option = telnet->_recv_sub_option;
		telnet_emit(telnet, &ev);
	} else {
		ev.type = TELNET_EV_DATA;
		telnet_emit(telnet, &ev);
	}
}

static void telnet_error(struct telnet *telnet, enum telnet_error code) {
	union telnet_event event;

	event.type = TELNET_EV_ERROR;
	event.error.code = code;
	telnet_emit(telnet, &event);
}

void telnet_send_negotiation(struct telnet *telnet, enum telnet_command cmd, unsigned char option) {
	unsigned char out[3];
	out[0] = TELNET_IAC;
	out[1] = cmd;
	out[2] = option;
	telnet_send_raw(telnet, out, sizeof(out));
}

static void telnet_send_command_raw(struct telnet *telnet, enum telnet_command command) {
	unsigned char out[2];

	out[0] = TELNET_IAC;
	out[1] = command;
	telnet_send_raw(telnet, out, 2);
}

void telnet_send_command(struct telnet *telnet, enum telnet_command command) {
	if (telnet->_send_sub_option != -1) {
		telnet->_send_sub_option = -1;
		telnet_send_command_raw(telnet, TELNET_CMD_SE);
	}

	telnet_send_command_raw(telnet, command);
}

static void telnet_send_escaped(struct telnet *telnet, const unsigned char *data, size_t size) {
	size_t start = 0, i;

	for (i = 0; i < size; i++) {
		if (data[i] != TELNET_IAC)
			continue;

		if (i > start)
			telnet_send_raw(telnet, data + start, i - start);

		telnet_send_command_raw(telnet, TELNET_CMD_ESC);
		start = i + 1;
	}

	if (start < size)
		telnet_send_raw(telnet, data + start, size - start);
}

void telnet_send_data(struct telnet *telnet, const unsigned char *data, size_t size) {
	if (telnet->_send_sub_option != -1) {
		telnet->_send_sub_option = -1;
		telnet_send_command_raw(telnet, TELNET_CMD_SE);
	}

	telnet_send_escaped(telnet, data, size);
}

void telnet_send_subnegotiation(struct telnet *telnet, unsigned char option, const unsigned char *data, size_t size) {
	if (telnet->_send_sub_option != option) {
		if (telnet->_send_sub_option != -1)
			/* if currently writing to a different subnegotiation, end that */
			telnet_send_command_raw(telnet, TELNET_CMD_SE);

		telnet->_send_sub_option = option;
		telnet_send_command_raw(telnet, TELNET_CMD_SB);
		telnet_send_raw(telnet, &option, 1);
	}

	telnet_send_escaped(telnet, data, size);
}

void telnet_send_subnegotiation_end(struct telnet *telnet, unsigned char option) {
	if (telnet->_send_sub_option != option)
		/* already ended */
		return;

	telnet->_send_sub_option = -1;
	telnet_send_command_raw(telnet, TELNET_CMD_SE);
}
static void telnet_handle_command(struct telnet *telnet, enum telnet_command cmd) {
	union telnet_event ev;
	unsigned char out[1];
	switch (cmd) {
		case TELNET_CMD_SE:
			if (telnet->_recv_sub_option == -1) {
				telnet_error(telnet, TELNET_ERR_INVALID_SE);
			} else {
				ev.type = TELNET_EV_SUBNEG;
				ev.data.buffer = NULL;
				ev.data.size = 0;
				ev.subneg.option = telnet->_recv_sub_option;
				telnet_emit(telnet, &ev);
			}

			telnet->_recv_sub_option = -1;
			telnet->_state = TELNET_STATE_DATA;
			break;

		case TELNET_CMD_SB:
			if (telnet->_recv_sub_option != -1) {
				telnet_error(telnet, TELNET_ERR_INVALID_SB);
				telnet->_state = TELNET_STATE_DATA;
				break;
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

		case TELNET_CMD_ESC:
			telnet->_state = TELNET_STATE_DATA;
			out[0] = cmd;
			telnet_write_raw(telnet, out, 1);
			break;

		default:
			telnet->_state = TELNET_STATE_DATA;
			ev.type = TELNET_EV_COMMAND;
			ev.command.code = cmd;
			telnet_emit(telnet, &ev);
			break;
	}
}

static void telnet_handle_negotiation(struct telnet *telnet, unsigned char option) {
	union telnet_event event;
	telnet->_state = TELNET_STATE_DATA;

	event.type = TELNET_EV_NEG;
	event.command.code = telnet->_command;
	event.neg.option = option;
	telnet_emit(telnet, &event);
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
