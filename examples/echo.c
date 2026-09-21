#include <minitelnet.h>
#include <stdio.h>

const char *option_state_str(enum telnet_option_state state) {
	switch (state) {
		case TELNET_OPTION_NO:
			return "NO";
		case TELNET_OPTION_YES:
			return "YES";
		case TELNET_OPTION_WANTNO:
			return "WANTNO";
		case TELNET_OPTION_WANTYES:
			return "WANTYES";
		case TELNET_OPTION_WANTNO_OPPOSITE:
			return "WANTNO_OPPOSITE";
		case TELNET_OPTION_WANTYES_OPPOSITE:
			return "WANTYES_OPPOSITE";
		case TELNET_OPTION_REQUEST_PENDING:
			return "REQUEST_PENDING";
		default:
			return "???";
	}
}

void handler(struct telnet *telnet, enum telnet_event_type type, const union telnet_event *event, void *userdata) {
	size_t i;

	(void) userdata;

	switch (type) {
		case TELNET_EV_COMMAND:
			fprintf(stderr, "telnet: command %02x issued\n", event->command);
			break;
		case TELNET_EV_SEND:
			fwrite(event->data.buffer, 1, event->data.size, stdout);
			fflush(stdout);
			break;
		case TELNET_EV_NEG:
			fprintf(stderr, "telnet: option %02x negotiated at %s from %s to %s\n",
			        event->neg.option,
			        event->neg.local ? "local" : "peer",
			        option_state_str(event->neg.old_state),
			        option_state_str(event->neg.new_state));
			if (event->neg.new_state == TELNET_OPTION_REQUEST_PENDING) {
				telnet_respond_negotiate(telnet, event->neg.local ? TELNET_CMD_WONT : TELNET_CMD_DONT, event->neg.option);
			}
			break;
		case TELNET_EV_SUBNEG:
			fprintf(stderr, "telnet: subnegotiation %02x at %zu:", event->subneg.option, event->subneg.offset);
			for (i = 0; i < event->subneg.size; i++) {
				fprintf(stderr, " %02x", event->subneg.buffer[i]);
			}
			fprintf(stderr, "\n");
			break;
		case TELNET_EV_DATA:
			fprintf(stderr, "telnet: data at %zu:", event->data.offset);
			for (i = 0; i < event->data.size; i++) {
				fprintf(stderr, " %02x", event->data.buffer[i]);
			}
			fprintf(stderr, "\n");
			telnet_send_data(telnet, event->data.buffer, event->data.size);
			break;
		case TELNET_EV_ERROR:
			fprintf(stderr, "telnet: error %02x\n", event->error);
			break;
	}
}

int main(void) {
	struct telnet telnet;
	unsigned char buffer[1024];
	size_t n;

	telnet_init(&telnet, handler, NULL);

	while ((n = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
		telnet_feed(&telnet, buffer, n);
	}

	return 0;
}
