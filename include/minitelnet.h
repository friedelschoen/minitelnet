#pragma once

#include <stddef.h>
#include <stdint.h>


enum telnet_state {
	TELNET_STATE_DATA,          /* currently receiving data */
	TELNET_STATE_COMMAND,       /* just received an IAC and expect a command */
	TELNET_STATE_OPTION,        /* received an WILL/WONT/DO/DONT command; expecting an option */
	TELNET_STATE_SUBNEG_OPTION, /* expecting subnegotiation option */
};

enum telnet_command {
	TELNET_CMD_SE = 0xf0, /* subnegotiation end */
	TELNET_CMD_NOP,       /* no operation */
	TELNET_CMD_DM,        /* data mark */
	TELNET_CMD_BRK,       /* break */
	TELNET_CMD_IP,        /* interrupt process */
	TELNET_CMD_AO,        /* abort output */
	TELNET_CMD_AYT,       /* are you there? */
	TELNET_CMD_EC,        /* erase character */
	TELNET_CMD_EL,        /* erase line */
	TELNET_CMD_GA,        /* go ahead */
	TELNET_CMD_SB,        /* subnegotiation begin */
	TELNET_CMD_WILL,      /* option accepted by us */
	TELNET_CMD_WONT,      /* option rejected by us */
	TELNET_CMD_DO,        /* option accepted by them */
	TELNET_CMD_DONT,      /* option rejected by them */
	TELNET_IAC            /* interpret-as-command / escape-character */
};

struct telnet;

enum telnet_event_type {
	TELNET_EV_COMMAND,  /* an arbitary command is issued */
	TELNET_EV_SEND,     /* request to send data to peer */
	TELNET_EV_NEG_THEM, /* an option is negotiated by them */
	TELNET_EV_NEG_US,   /* an option is negotiated and acknogledged by them */
	TELNET_EV_SUBNEG,   /* subnegotiation data is sent */
	TELNET_EV_DATA,     /* data is sent */
	TELNET_EV_ERROR,    /* an error occurred :( */
};

enum telnet_error {
	TELNET_ERROR_INVALID_SB, /* an subnegotiation begin ocurred while already in subn */
	TELNET_ERROR_INVALID_SE, /* an subnegotiation end ocurred while already in data */
};

enum telnet_option_state {
	TELNET_OPTION_DISABLED,
	TELNET_OPTION_ENABLED,
	TELNET_OPTION_WANT_DISABLED,
	TELNET_OPTION_WANT_ENABLED,
	TELNET_OPTION_THEY_WANT_DISABLED,
	TELNET_OPTION_THEY_WANT_ENABLED,
};

struct telnet_option {
	uint8_t us;
	uint8_t them;
};

struct telnet_event_data {
	const void *buffer;
	size_t size;
};

struct telnet_event_subneg {
	const void *buffer;
	size_t size;
	uint8_t option; /* option at last, so you could use .data */
};

struct telnet_event_negotiate {
	enum telnet_command command;
	uint8_t option;
};

union telnet_event {
	enum telnet_error error;
	struct telnet_event_data data;
	struct telnet_event_subneg subneg;
	struct telnet_event_negotiate neg;
	enum telnet_command command;
};

typedef void (*telnet_handler_t)(struct telnet *telnet, enum telnet_event_type type, const union telnet_event *event, void *userdata);

struct telnet {
	telnet_handler_t _handler;
	void *_userdata;

	/* current receiving state */
	enum telnet_state _state;
	/* if state == OPTION, _command is set to WILL/WONT/DO/DONT */
	enum telnet_command _command;
	/* if currently receiving a subnegotiation, this is set to its option; otherwise -1. */
	int _recv_sub_option;
	/* if currently sending a subnegotiation, this is set to its option; otherwise -1. */
	int _send_sub_option;

	/* here are the current option negotiations noted */
	struct telnet_option options[256];
};

/* initalizes state */
void telnet_init(struct telnet *telnet, telnet_handler_t handler, void *userdata);

/* feed data into state machine with just received data */
void telnet_feed(struct telnet *telnet, const void *data, size_t size);

/* send data to peer */
void telnet_send_data(struct telnet *telnet, const void *data, size_t size);

/* send data to peer */
void telnet_send_subnegotiation(struct telnet *telnet, uint8_t option, const void *data, size_t size);

/* explicit subneogotiation end */
void telnet_send_subnegotiation_end(struct telnet *telnet, uint8_t option);

/* send short command */
void telnet_send_command(struct telnet *telnet, enum telnet_command command);

/* send negotiation */
void telnet_send_negotiate(struct telnet *telnet, enum telnet_command command, uint8_t option);
