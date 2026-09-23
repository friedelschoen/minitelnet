#include <minitelnet.h>
#include <stdio.h>

static const char *command_str(enum telnet_command command) {
	switch (command) {
		case TELNET_CMD_WILL:
			return "WILL";
		case TELNET_CMD_WONT:
			return "WONT";
		case TELNET_CMD_DO:
			return "DO";
		case TELNET_CMD_DONT:
			return "DONT";
		default:
			return "???";
	}
}

static void handler(struct telnet *telnet,
                    const union telnet_event *event,
                    void *userdata) {
	size_t i;

	(void) userdata;

	switch (event->type) {
		case TELNET_EV_COMMAND:
			fprintf(stderr,
			        "telnet: command %02x issued\n",
			        event->command.code);
			break;

		case TELNET_EV_SEND:
			fwrite(event->data.buffer, 1, event->data.size, stdout);
			fflush(stdout);
			break;

		case TELNET_EV_NEG:
			fprintf(stderr,
			        "telnet: negotiation %s %02x\n",
			        command_str(event->command.code),
			        event->neg.option);

			switch (event->command.code) {
				case TELNET_CMD_WILL:
					telnet_send_negotiation(
					    telnet,
					    TELNET_CMD_DONT,
					    event->neg.option);
					break;

				case TELNET_CMD_DO:
					telnet_send_negotiation(
					    telnet,
					    TELNET_CMD_WONT,
					    event->neg.option);
					break;

				case TELNET_CMD_WONT:
				case TELNET_CMD_DONT:
				default:
					break;
			}
			break;

		case TELNET_EV_SUBNEG:
			fprintf(stderr,
			        "telnet: subnegotiation %02x:",
			        event->subneg.option);

			for (i = 0; i < event->data.size; ++i)
				fprintf(stderr, " %02x", event->data.buffer[i]);

			fprintf(stderr, "\n");
			break;

		case TELNET_EV_DATA:
			fprintf(stderr, "telnet: data:");

			for (i = 0; i < event->data.size; ++i)
				fprintf(stderr, " %02x", event->data.buffer[i]);

			fprintf(stderr, "\n");

			telnet_send_data(
			    telnet,
			    event->data.buffer,
			    event->data.size);
			break;

		case TELNET_EV_ERROR:
			fprintf(stderr,
			        "telnet: error %02x\n",
			        event->error.code);
			break;
	}
}

int main(void) {
	struct telnet telnet;
	unsigned char buffer[1024];
	size_t n;

	telnet_init(&telnet, handler, NULL);

	while ((n = fread(buffer, 1, sizeof(buffer), stdin)) > 0)
		telnet_feed(&telnet, buffer, n);

	return 0;
}
