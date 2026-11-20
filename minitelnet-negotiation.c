#include <minitelnet-negotiation.h>
#include <string.h>

enum telnet_negotiation_state telnet_negotiation_local(const telnet_negotiation_t neg, unsigned char option) {
	return (enum telnet_negotiation_state)(neg[option] & 0x0f);
}

enum telnet_negotiation_state telnet_negotiation_peer(const telnet_negotiation_t neg, unsigned char option) {
	return (enum telnet_negotiation_state)((neg[option] & 0xf0) >> 4);
}

static enum telnet_negotiation_state telnet_get_state(const telnet_negotiation_t neg, unsigned char option, int local) {
	unsigned int shift = local ? 0 : 4;

	return (enum telnet_negotiation_state)((neg[option] >> shift) & 0x0f);
}

static int telnet_set_state(telnet_negotiation_t neg, unsigned char option, int local, enum telnet_negotiation_state state, struct telnet_negotiation_transition *trns) {
	unsigned int shift = local ? 0 : 4;
	unsigned char mask = (unsigned char) (0x0f << shift);

	neg[option] = (neg[option] & (unsigned char) ~mask) |
	              (unsigned char) ((unsigned int) state << shift);

	trns->option = option;
	trns->local = local;
	trns->state = state;

	return 1;
}

int telnet_negotiation_feed(telnet_negotiation_t neg, enum telnet_command cmd, unsigned char option, struct telnet_negotiation_transition *trns) {
	int local, enable;
	enum telnet_negotiation_state state;

	memset(trns, 0, sizeof(*trns));

	local = cmd == TELNET_CMD_DO || cmd == TELNET_CMD_DONT;
	enable = cmd == TELNET_CMD_WILL || cmd == TELNET_CMD_DO;
	state = telnet_get_state(neg, option, local);

	if (enable) {
		switch (state) {
			case TELNET_OPTION_NO:
				/* NO -> YES: need permission so make it pending */
				state = TELNET_OPTION_PENDING;
				break;

			case TELNET_OPTION_WANTYES:
				state = TELNET_OPTION_YES;
				break;

			case TELNET_OPTION_WANTNO:
				trns->error = 1;
				state = TELNET_OPTION_NO;
				break;

			case TELNET_OPTION_WANTYES_OPPOSITE:
				trns->outgoing = local
				                     ? TELNET_CMD_WONT
				                     : TELNET_CMD_DONT;
				state = TELNET_OPTION_WANTNO;
				break;

			case TELNET_OPTION_WANTNO_OPPOSITE:
				trns->error = 1;
				state = TELNET_OPTION_YES;
				break;

			default:
				return 0;
		}
	} else {
		switch (state) {
			case TELNET_OPTION_YES:
				trns->outgoing = local
				                     ? TELNET_CMD_WONT
				                     : TELNET_CMD_DONT;
				state = TELNET_OPTION_NO;
				break;

			case TELNET_OPTION_WANTNO_OPPOSITE:
				trns->outgoing = local
				                     ? TELNET_CMD_WILL
				                     : TELNET_CMD_DO;
				state = TELNET_OPTION_WANTYES;
				break;

			case TELNET_OPTION_NO:
				return 0;

			default:
				state = TELNET_OPTION_NO;
				break;
		}
	}

	return telnet_set_state(neg, option, local, state, trns);
}

int telnet_negotiation_respond(telnet_negotiation_t neg, enum telnet_command cmd, unsigned char option, struct telnet_negotiation_transition *trns) {
	int local, enable;

	memset(trns, 0, sizeof(*trns));

	local = cmd == TELNET_CMD_WILL || cmd == TELNET_CMD_WONT;
	enable = cmd == TELNET_CMD_WILL || cmd == TELNET_CMD_DO;
	if (telnet_get_state(neg, option, local) != TELNET_OPTION_PENDING)
		return 0;

	trns->outgoing = cmd;

	return telnet_set_state(neg, option, local, enable ? TELNET_OPTION_YES : TELNET_OPTION_NO, trns);
}
int telnet_negotiation_request(telnet_negotiation_t neg, enum telnet_command cmd, unsigned char option, struct telnet_negotiation_transition *trns) {
	int local, enable;
	enum telnet_negotiation_state state;

	memset(trns, 0, sizeof(*trns));

	local = cmd == TELNET_CMD_WILL || cmd == TELNET_CMD_WONT;
	enable = cmd == TELNET_CMD_WILL || cmd == TELNET_CMD_DO;
	state = telnet_get_state(neg, option, local);

	if (enable) {
		switch (state) {
			case TELNET_OPTION_NO:
				trns->outgoing = cmd;
				state = TELNET_OPTION_WANTYES;
				break;

			case TELNET_OPTION_WANTNO:
				state = TELNET_OPTION_WANTNO_OPPOSITE;
				break;

			case TELNET_OPTION_WANTYES_OPPOSITE:
				state = TELNET_OPTION_WANTYES;
				break;

			default:
				return 0;
		}
	} else {
		switch (state) {
			case TELNET_OPTION_YES:
				trns->outgoing = cmd;
				state = TELNET_OPTION_WANTNO;
				break;

			case TELNET_OPTION_WANTYES:
				state = TELNET_OPTION_WANTYES_OPPOSITE;
				break;

			case TELNET_OPTION_WANTNO_OPPOSITE:
				state = TELNET_OPTION_WANTNO;
				break;

			default:
				return 0;
		}
	}

	return telnet_set_state(neg, option, local, state, trns);
}
