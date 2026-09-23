#include <minitelnet-negotiation.h>
#include <string.h>

enum telnet_negotiation_state telnet_negotiation_local(const telnet_negotiation_t neg, unsigned char option) {
	return (enum telnet_negotiation_state)(neg[option] & 0x0f);
}

enum telnet_negotiation_state telnet_negotiation_peer(const telnet_negotiation_t neg, unsigned char option) {
	return (enum telnet_negotiation_state)((neg[option] & 0xf0) >> 4);
}

static int telnet_set_option_local(telnet_negotiation_t neg, unsigned char option, enum telnet_negotiation_state state, struct telnet_negotiation_transition *trns) {
	neg[option] &= 0xf0;
	neg[option] |= (unsigned char) state;

	trns->option = option;
	trns->local = 1;
	trns->state = state;
	return 1;
}

static int telnet_set_option_peer(telnet_negotiation_t neg, unsigned char option, enum telnet_negotiation_state state, struct telnet_negotiation_transition *trns) {
	neg[option] &= 0x0f;
	neg[option] |= (unsigned char) state << 4;

	trns->option = option;
	trns->local = 0;
	trns->state = state;
	return 1;
}

int telnet_negotiation_feed(telnet_negotiation_t neg, enum telnet_command cmd, unsigned char option, struct telnet_negotiation_transition *trns) {
	enum telnet_negotiation_state local = telnet_negotiation_local(neg, option),
	                              peer = telnet_negotiation_peer(neg, option);

	memset(trns, 0, sizeof(*trns));

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
				return telnet_set_option_peer(neg, option, TELNET_OPTION_REQUEST_PENDING, trns);
			case TELNET_OPTION_WANTYES:
				return telnet_set_option_peer(neg, option, TELNET_OPTION_YES, trns);
			case TELNET_OPTION_WANTNO:
				trns->error = 1;
				return telnet_set_option_peer(neg, option, TELNET_OPTION_NO, trns);
			case TELNET_OPTION_WANTYES_OPPOSITE:
				trns->outgoing = TELNET_CMD_DONT;
				return telnet_set_option_peer(neg, option, TELNET_OPTION_WANTNO, trns);
			case TELNET_OPTION_WANTNO_OPPOSITE:
				trns->error = 1;
				return telnet_set_option_peer(neg, option, TELNET_OPTION_YES, trns);
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
				trns->outgoing = TELNET_CMD_DONT;
				return telnet_set_option_peer(neg, option, TELNET_OPTION_NO, trns);
			case TELNET_OPTION_NO:
				/* ignore */
				break;
			case TELNET_OPTION_WANTYES:
				return telnet_set_option_peer(neg, option, TELNET_OPTION_NO, trns);
			case TELNET_OPTION_WANTNO:
				return telnet_set_option_peer(neg, option, TELNET_OPTION_NO, trns);
			case TELNET_OPTION_WANTYES_OPPOSITE:
				return telnet_set_option_peer(neg, option, TELNET_OPTION_NO, trns);
			case TELNET_OPTION_WANTNO_OPPOSITE:
				trns->outgoing = TELNET_CMD_DO;
				return telnet_set_option_peer(neg, option, TELNET_OPTION_WANTYES, trns);
			case TELNET_OPTION_REQUEST_PENDING:
				return telnet_set_option_peer(neg, option, TELNET_OPTION_NO, trns);
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
				return telnet_set_option_local(neg, option, TELNET_OPTION_REQUEST_PENDING, trns);
			case TELNET_OPTION_WANTYES:
				return telnet_set_option_local(neg, option, TELNET_OPTION_YES, trns);
			case TELNET_OPTION_WANTNO:
				trns->error = 1;
				return telnet_set_option_local(neg, option, TELNET_OPTION_NO, trns);
			case TELNET_OPTION_WANTYES_OPPOSITE:
				trns->outgoing = TELNET_CMD_WONT;
				return telnet_set_option_local(neg, option, TELNET_OPTION_WANTNO, trns);
			case TELNET_OPTION_WANTNO_OPPOSITE:
				trns->error = 1;
				return telnet_set_option_local(neg, option, TELNET_OPTION_YES, trns);
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
				trns->outgoing = TELNET_CMD_WONT;
				return telnet_set_option_local(neg, option, TELNET_OPTION_NO, trns);
			case TELNET_OPTION_NO:
				/* ignore */
				break;
			case TELNET_OPTION_WANTYES:
				return telnet_set_option_local(neg, option, TELNET_OPTION_NO, trns);
			case TELNET_OPTION_WANTNO:
				return telnet_set_option_local(neg, option, TELNET_OPTION_NO, trns);
				break;
			case TELNET_OPTION_WANTYES_OPPOSITE:
				return telnet_set_option_local(neg, option, TELNET_OPTION_NO, trns);
			case TELNET_OPTION_WANTNO_OPPOSITE:
				trns->outgoing = TELNET_CMD_WILL;
				return telnet_set_option_local(neg, option, TELNET_OPTION_WANTYES, trns);
			case TELNET_OPTION_REQUEST_PENDING:
				return telnet_set_option_local(neg, option, TELNET_OPTION_NO, trns);
		}
	}
	return 0;
}

int telnet_negotiation_respond(telnet_negotiation_t neg, enum telnet_command command, unsigned char option, struct telnet_negotiation_transition *trns) {
	enum telnet_negotiation_state local = telnet_negotiation_local(neg, option),
	                              peer = telnet_negotiation_peer(neg, option);

	memset(trns, 0, sizeof(*trns));

	switch (command) {
		case TELNET_CMD_WILL:
		case TELNET_CMD_WONT:
			if (local != TELNET_OPTION_REQUEST_PENDING)
				return 0;

			trns->outgoing = command;
			return telnet_set_option_local(neg, option, command == TELNET_CMD_WILL ? TELNET_OPTION_YES : TELNET_OPTION_NO, trns);

		case TELNET_CMD_DO:
		case TELNET_CMD_DONT:
			if (peer != TELNET_OPTION_REQUEST_PENDING)
				return 0;

			trns->outgoing = command;
			return telnet_set_option_peer(neg, option, command == TELNET_CMD_DO ? TELNET_OPTION_YES : TELNET_OPTION_NO, trns);
		default:
			return 0;
	}
}

int telnet_negotiation_send(telnet_negotiation_t neg, enum telnet_command command, unsigned char option, struct telnet_negotiation_transition *trns) {
	enum telnet_negotiation_state local = telnet_negotiation_local(neg, option),
	                              peer = telnet_negotiation_peer(neg, option);

	memset(trns, 0, sizeof(*trns));

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
				trns->outgoing = TELNET_CMD_DO;
				return telnet_set_option_peer(neg, option, TELNET_OPTION_WANTYES, trns);
			case TELNET_OPTION_YES:
				/* already enabled */
				break;
			case TELNET_OPTION_WANTNO:
				return telnet_set_option_peer(neg, option, TELNET_OPTION_WANTNO_OPPOSITE, trns);
			case TELNET_OPTION_WANTYES_OPPOSITE:
				return telnet_set_option_peer(neg, option, TELNET_OPTION_WANTYES, trns);
			case TELNET_OPTION_WANTYES:
			case TELNET_OPTION_WANTNO_OPPOSITE:
				/* desired state is already enable */
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
				trns->outgoing = TELNET_CMD_DONT;
				return telnet_set_option_peer(neg, option, TELNET_OPTION_WANTNO, trns);
			case TELNET_OPTION_WANTYES:
				return telnet_set_option_peer(neg, option, TELNET_OPTION_WANTYES_OPPOSITE, trns);
			case TELNET_OPTION_WANTNO_OPPOSITE:
				return telnet_set_option_peer(neg, option, TELNET_OPTION_WANTNO, trns);
			case TELNET_OPTION_WANTNO:
			case TELNET_OPTION_WANTYES_OPPOSITE:
				/* desired state is already enable */
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
				trns->outgoing = TELNET_CMD_WILL;
				return telnet_set_option_local(neg, option, TELNET_OPTION_WANTYES, trns);
			case TELNET_OPTION_YES:
				/* already enabled */
				break;
			case TELNET_OPTION_WANTNO:
				return telnet_set_option_local(neg, option, TELNET_OPTION_WANTNO_OPPOSITE, trns);
			case TELNET_OPTION_WANTYES_OPPOSITE:
				return telnet_set_option_local(neg, option, TELNET_OPTION_WANTYES, trns);
			case TELNET_OPTION_WANTYES:
			case TELNET_OPTION_WANTNO_OPPOSITE:
				/* desired state is already enable */
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
				trns->outgoing = TELNET_CMD_WONT;
				return telnet_set_option_local(neg, option, TELNET_OPTION_WANTNO, trns);
			case TELNET_OPTION_WANTYES:
				return telnet_set_option_local(neg, option, TELNET_OPTION_WANTYES_OPPOSITE, trns);
			case TELNET_OPTION_WANTNO_OPPOSITE:
				return telnet_set_option_local(neg, option, TELNET_OPTION_WANTNO, trns);
			case TELNET_OPTION_WANTNO:
			case TELNET_OPTION_WANTYES_OPPOSITE:
				/* desired state is already enable */
				break;
			case TELNET_OPTION_REQUEST_PENDING:
				break;
		}
	}
	return 0;
}
