/* SPDX-License-Identifier: Zlib */
/* Copyright (c) 2026 Friedel Schön */

#include <check.h>
#include <minitelnet.h>
#include <stddef.h>
#include <string.h>

#define OPTION 42

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct test_context {
	unsigned char output[64];
	size_t output_size;

	enum telnet_error errors[16];
	size_t error_count;
};

static void event_handler(struct telnet *telnet,
                          enum telnet_event_type type,
                          const union telnet_event *event,
                          void *userdata) {
	struct test_context *ctx = userdata;

	(void) telnet;

	switch (type) {
		case TELNET_EV_SEND:
			ck_assert_msg(
			    ctx->output_size + event->data.size <= sizeof(ctx->output),
			    "test output buffer overflow");

			memcpy(ctx->output + ctx->output_size,
			       event->data.buffer,
			       event->data.size);

			ctx->output_size += event->data.size;
			break;

		case TELNET_EV_ERROR:
			ck_assert_msg(
			    ctx->error_count < ARRAY_SIZE(ctx->errors),
			    "test error buffer overflow");

			ctx->errors[ctx->error_count++] = event->error;
			break;

		default:
			break;
	}
}

static void init_telnet(struct telnet *telnet, struct test_context *ctx) {
	memset(ctx, 0, sizeof(*ctx));
	telnet_init(telnet, event_handler, ctx);
}

/*
 * Test-only state injection.
 *
 * Keep knowledge of minitelnet's internal nibble representation confined
 * to these helpers. Assertions use the public state accessors.
 */
static void set_local(struct telnet *telnet,
                      unsigned char option,
                      enum telnet_option_state state) {
	telnet->_options[option] =
	    (unsigned char) ((telnet->_options[option] & 0xf0) |
	                     ((unsigned char) state & 0x0f));
}

static void set_peer(struct telnet *telnet,
                     unsigned char option,
                     enum telnet_option_state state) {
	telnet->_options[option] =
	    (unsigned char) ((telnet->_options[option] & 0x0f) |
	                     (((unsigned char) state & 0x0f) << 4));
}

static enum telnet_option_state get_state(const struct telnet *telnet,
                                          unsigned char option,
                                          int local) {
	if (local)
		return telnet_option_local(telnet, option);

	return telnet_option_peer(telnet, option);
}

static void set_state(struct telnet *telnet,
                      unsigned char option,
                      int local,
                      enum telnet_option_state state) {
	if (local)
		set_local(telnet, option, state);
	else
		set_peer(telnet, option, state);
}

/*
 * Outgoing negotiation
 */

struct send_case {
	const char *name;

	enum telnet_command command;
	int local;

	enum telnet_option_state initial;
	enum telnet_option_state expected;

	int sends_command;
	int emits_error;
};

static const struct send_case send_cases[] = {
	/*
	 * DO -- request enabling a peer option.
	 */
	{
	    "DO: NO -> WANTYES",
	    TELNET_CMD_DO, 0,
	    TELNET_OPTION_NO,
	    TELNET_OPTION_WANTYES,
	    1, 0 },
	{ "DO: YES -> YES",
	  TELNET_CMD_DO, 0,
	  TELNET_OPTION_YES,
	  TELNET_OPTION_YES,
	  0, 0 },
	{ "DO: WANTNO -> WANTNO_OPPOSITE",
	  TELNET_CMD_DO, 0,
	  TELNET_OPTION_WANTNO,
	  TELNET_OPTION_WANTNO_OPPOSITE,
	  0, 0 },
	{ "DO: WANTYES -> WANTYES",
	  TELNET_CMD_DO, 0,
	  TELNET_OPTION_WANTYES,
	  TELNET_OPTION_WANTYES,
	  0, 1 },
	{ "DO: WANTNO_OPPOSITE -> WANTNO_OPPOSITE",
	  TELNET_CMD_DO, 0,
	  TELNET_OPTION_WANTNO_OPPOSITE,
	  TELNET_OPTION_WANTNO_OPPOSITE,
	  0, 1 },
	{ "DO: WANTYES_OPPOSITE -> WANTYES",
	  TELNET_CMD_DO, 0,
	  TELNET_OPTION_WANTYES_OPPOSITE,
	  TELNET_OPTION_WANTYES,
	  0, 0 },
	{ "DO: REQUEST_PENDING -> REQUEST_PENDING",
	  TELNET_CMD_DO, 0,
	  TELNET_OPTION_REQUEST_PENDING,
	  TELNET_OPTION_REQUEST_PENDING,
	  0, 0 },

	/*
	 * DONT -- request disabling a peer option.
	 */
	{
	    "DONT: NO -> NO",
	    TELNET_CMD_DONT, 0,
	    TELNET_OPTION_NO,
	    TELNET_OPTION_NO,
	    0, 0 },
	{ "DONT: YES -> WANTNO",
	  TELNET_CMD_DONT, 0,
	  TELNET_OPTION_YES,
	  TELNET_OPTION_WANTNO,
	  1, 0 },
	{ "DONT: WANTNO -> WANTNO",
	  TELNET_CMD_DONT, 0,
	  TELNET_OPTION_WANTNO,
	  TELNET_OPTION_WANTNO,
	  0, 1 },
	{ "DONT: WANTYES -> WANTYES_OPPOSITE",
	  TELNET_CMD_DONT, 0,
	  TELNET_OPTION_WANTYES,
	  TELNET_OPTION_WANTYES_OPPOSITE,
	  0, 0 },
	{ "DONT: WANTNO_OPPOSITE -> WANTNO",
	  TELNET_CMD_DONT, 0,
	  TELNET_OPTION_WANTNO_OPPOSITE,
	  TELNET_OPTION_WANTNO,
	  0, 0 },
	{ "DONT: WANTYES_OPPOSITE -> WANTYES_OPPOSITE",
	  TELNET_CMD_DONT, 0,
	  TELNET_OPTION_WANTYES_OPPOSITE,
	  TELNET_OPTION_WANTYES_OPPOSITE,
	  0, 1 },
	{ "DONT: REQUEST_PENDING -> REQUEST_PENDING",
	  TELNET_CMD_DONT, 0,
	  TELNET_OPTION_REQUEST_PENDING,
	  TELNET_OPTION_REQUEST_PENDING,
	  0, 0 },

	/*
	 * WILL -- request enabling a local option.
	 */
	{
	    "WILL: NO -> WANTYES",
	    TELNET_CMD_WILL, 1,
	    TELNET_OPTION_NO,
	    TELNET_OPTION_WANTYES,
	    1, 0 },
	{ "WILL: YES -> YES",
	  TELNET_CMD_WILL, 1,
	  TELNET_OPTION_YES,
	  TELNET_OPTION_YES,
	  0, 0 },
	{ "WILL: WANTNO -> WANTNO_OPPOSITE",
	  TELNET_CMD_WILL, 1,
	  TELNET_OPTION_WANTNO,
	  TELNET_OPTION_WANTNO_OPPOSITE,
	  0, 0 },
	{ "WILL: WANTYES -> WANTYES",
	  TELNET_CMD_WILL, 1,
	  TELNET_OPTION_WANTYES,
	  TELNET_OPTION_WANTYES,
	  0, 1 },
	{ "WILL: WANTNO_OPPOSITE -> WANTNO_OPPOSITE",
	  TELNET_CMD_WILL, 1,
	  TELNET_OPTION_WANTNO_OPPOSITE,
	  TELNET_OPTION_WANTNO_OPPOSITE,
	  0, 1 },
	{ "WILL: WANTYES_OPPOSITE -> WANTYES",
	  TELNET_CMD_WILL, 1,
	  TELNET_OPTION_WANTYES_OPPOSITE,
	  TELNET_OPTION_WANTYES,
	  0, 0 },
	{ "WILL: REQUEST_PENDING -> REQUEST_PENDING",
	  TELNET_CMD_WILL, 1,
	  TELNET_OPTION_REQUEST_PENDING,
	  TELNET_OPTION_REQUEST_PENDING,
	  0, 0 },

	/*
	 * WONT -- request disabling a local option.
	 */
	{
	    "WONT: NO -> NO",
	    TELNET_CMD_WONT, 1,
	    TELNET_OPTION_NO,
	    TELNET_OPTION_NO,
	    0, 0 },
	{ "WONT: YES -> WANTNO",
	  TELNET_CMD_WONT, 1,
	  TELNET_OPTION_YES,
	  TELNET_OPTION_WANTNO,
	  1, 0 },
	{ "WONT: WANTNO -> WANTNO",
	  TELNET_CMD_WONT, 1,
	  TELNET_OPTION_WANTNO,
	  TELNET_OPTION_WANTNO,
	  0, 1 },
	{ "WONT: WANTYES -> WANTYES_OPPOSITE",
	  TELNET_CMD_WONT, 1,
	  TELNET_OPTION_WANTYES,
	  TELNET_OPTION_WANTYES_OPPOSITE,
	  0, 0 },
	{ "WONT: WANTNO_OPPOSITE -> WANTNO",
	  TELNET_CMD_WONT, 1,
	  TELNET_OPTION_WANTNO_OPPOSITE,
	  TELNET_OPTION_WANTNO,
	  0, 0 },
	{ "WONT: WANTYES_OPPOSITE -> WANTYES_OPPOSITE",
	  TELNET_CMD_WONT, 1,
	  TELNET_OPTION_WANTYES_OPPOSITE,
	  TELNET_OPTION_WANTYES_OPPOSITE,
	  0, 1 },
	{ "WONT: REQUEST_PENDING -> REQUEST_PENDING",
	  TELNET_CMD_WONT, 1,
	  TELNET_OPTION_REQUEST_PENDING,
	  TELNET_OPTION_REQUEST_PENDING,
	  0, 0 }
};

START_TEST(test_send_negotiation_table) {
	size_t i;

	for (i = 0; i < ARRAY_SIZE(send_cases); ++i) {
		const struct send_case *tc = &send_cases[i];
		struct telnet telnet;
		struct test_context ctx;
		enum telnet_option_state actual;

		init_telnet(&telnet, &ctx);
		set_state(&telnet, OPTION, tc->local, tc->initial);

		telnet_send_negotiation(&telnet, tc->command, OPTION);

		actual = get_state(&telnet, OPTION, tc->local);

		ck_assert_msg(
		    actual == tc->expected,
		    "%s: expected state %d, got %d",
		    tc->name,
		    (int) tc->expected,
		    (int) actual);

		ck_assert_msg(
		    ctx.output_size == (tc->sends_command ? 3u : 0u),
		    "%s: expected %u output bytes, got %lu",
		    tc->name,
		    tc->sends_command ? 3u : 0u,
		    (unsigned long) ctx.output_size);

		ck_assert_msg(
		    ctx.error_count == (tc->emits_error ? 1u : 0u),
		    "%s: expected %u errors, got %lu",
		    tc->name,
		    tc->emits_error ? 1u : 0u,
		    (unsigned long) ctx.error_count);

		if (tc->sends_command) {
			ck_assert_msg(
			    ctx.output[0] == TELNET_IAC,
			    "%s: expected IAC", tc->name);

			ck_assert_msg(
			    ctx.output[1] == (unsigned char) tc->command,
			    "%s: expected command %d, got %u",
			    tc->name,
			    (int) tc->command,
			    (unsigned int) ctx.output[1]);

			ck_assert_msg(
			    ctx.output[2] == OPTION,
			    "%s: expected option %d, got %u",
			    tc->name,
			    OPTION,
			    (unsigned int) ctx.output[2]);
		}
	}
}
END_TEST

/*
 * Initial incoming negotiation.
 *
 * Starting from NO:
 *
 *     WILL -> peer REQUEST_PENDING
 *     DO   -> local REQUEST_PENDING
 *     WONT -> peer NO
 *     DONT -> local NO
 */

struct receive_case {
	const char *name;
	enum telnet_command command;
	int local;
	enum telnet_option_state expected;
};

static const struct receive_case receive_cases[] = {
	{ "WILL from NO",
	  TELNET_CMD_WILL,
	  0,
	  TELNET_OPTION_REQUEST_PENDING },
	{ "WONT from NO",
	  TELNET_CMD_WONT,
	  0,
	  TELNET_OPTION_NO },
	{ "DO from NO",
	  TELNET_CMD_DO,
	  1,
	  TELNET_OPTION_REQUEST_PENDING },
	{ "DONT from NO",
	  TELNET_CMD_DONT,
	  1,
	  TELNET_OPTION_NO }
};

START_TEST(test_receive_initial_negotiation) {
	size_t i;

	for (i = 0; i < ARRAY_SIZE(receive_cases); ++i) {
		const struct receive_case *tc = &receive_cases[i];
		struct telnet telnet;
		struct test_context ctx;
		enum telnet_option_state actual;
		unsigned char input[3];

		init_telnet(&telnet, &ctx);

		input[0] = TELNET_IAC;
		input[1] = (unsigned char) tc->command;
		input[2] = OPTION;

		telnet_feed(&telnet, input, sizeof(input));

		actual = get_state(&telnet, OPTION, tc->local);

		ck_assert_msg(
		    actual == tc->expected,
		    "%s: expected state %d, got %d",
		    tc->name,
		    (int) tc->expected,
		    (int) actual);
	}
}
END_TEST

/*
 * Responding to an asynchronously pending enable request.
 */

struct respond_case {
	const char *name;
	enum telnet_command command;
	int local;
	enum telnet_option_state expected;
};

static const struct respond_case respond_cases[] = {
	{ "accept DO with WILL",
	  TELNET_CMD_WILL,
	  1,
	  TELNET_OPTION_YES },
	{ "reject DO with WONT",
	  TELNET_CMD_WONT,
	  1,
	  TELNET_OPTION_NO },
	{ "accept WILL with DO",
	  TELNET_CMD_DO,
	  0,
	  TELNET_OPTION_YES },
	{ "reject WILL with DONT",
	  TELNET_CMD_DONT,
	  0,
	  TELNET_OPTION_NO }
};

START_TEST(test_respond_negotiation) {
	size_t i;

	for (i = 0; i < ARRAY_SIZE(respond_cases); ++i) {
		const struct respond_case *tc = &respond_cases[i];
		struct telnet telnet;
		struct test_context ctx;
		enum telnet_option_state actual;

		init_telnet(&telnet, &ctx);

		set_state(&telnet,
		          OPTION,
		          tc->local,
		          TELNET_OPTION_REQUEST_PENDING);

		telnet_respond_negotiation(&telnet, tc->command, OPTION);

		actual = get_state(&telnet, OPTION, tc->local);

		ck_assert_msg(
		    actual == tc->expected,
		    "%s: expected state %d, got %d",
		    tc->name,
		    (int) tc->expected,
		    (int) actual);

		ck_assert_msg(
		    ctx.output_size == 3,
		    "%s: expected 3 output bytes, got %lu",
		    tc->name,
		    (unsigned long) ctx.output_size);

		ck_assert_msg(
		    ctx.output[0] == TELNET_IAC,
		    "%s: expected IAC",
		    tc->name);

		ck_assert_msg(
		    ctx.output[1] == (unsigned char) tc->command,
		    "%s: expected command %d, got %u",
		    tc->name,
		    (int) tc->command,
		    (unsigned int) ctx.output[1]);

		ck_assert_msg(
		    ctx.output[2] == OPTION,
		    "%s: expected option %d, got %u",
		    tc->name,
		    OPTION,
		    (unsigned int) ctx.output[2]);

		ck_assert_msg(
		    ctx.error_count == 0,
		    "%s: unexpected error",
		    tc->name);
	}
}
END_TEST

/*
 * A stale response must not start a new negotiation.
 *
 * This is the important distinction between telnet_respond_negotiation()
 * and telnet_send_negotiation().
 */
START_TEST(test_stale_response_does_nothing) {
	static const enum telnet_command commands[] = {
		TELNET_CMD_WILL,
		TELNET_CMD_WONT,
		TELNET_CMD_DO,
		TELNET_CMD_DONT
	};

	size_t i;

	for (i = 0; i < ARRAY_SIZE(commands); ++i) {
		struct telnet telnet;
		struct test_context ctx;
		int local;

		init_telnet(&telnet, &ctx);

		local = commands[i] == TELNET_CMD_WILL ||
		        commands[i] == TELNET_CMD_WONT;

		telnet_respond_negotiation(&telnet, commands[i], OPTION);

		ck_assert_int_eq(
		    get_state(&telnet, OPTION, local),
		    TELNET_OPTION_NO);

		ck_assert_uint_eq(ctx.output_size, 0);
		ck_assert_uint_eq(ctx.error_count, 0);
	}
}
END_TEST

/*
 * If the peer withdraws an enable request before the application answers,
 * a later application response must be stale and therefore have no effect.
 */
START_TEST(test_withdrawn_request_makes_response_stale) {
	struct telnet telnet;
	struct test_context ctx;
	unsigned char request[] = {
		TELNET_IAC, TELNET_CMD_WILL, OPTION
	};
	unsigned char withdraw[] = {
		TELNET_IAC, TELNET_CMD_WONT, OPTION
	};

	init_telnet(&telnet, &ctx);

	telnet_feed(&telnet, request, sizeof(request));

	ck_assert_int_eq(
	    telnet_option_peer(&telnet, OPTION),
	    TELNET_OPTION_REQUEST_PENDING);

	telnet_feed(&telnet, withdraw, sizeof(withdraw));

	ck_assert_int_eq(
	    telnet_option_peer(&telnet, OPTION),
	    TELNET_OPTION_NO);

	/*
	 * Discard any output generated while processing the incoming
	 * negotiation. We only care whether the stale response produces
	 * additional output.
	 */
	ctx.output_size = 0;
	ctx.error_count = 0;

	telnet_respond_negotiation(&telnet, TELNET_CMD_DO, OPTION);

	ck_assert_int_eq(
	    telnet_option_peer(&telnet, OPTION),
	    TELNET_OPTION_NO);

	ck_assert_uint_eq(ctx.output_size, 0);
	ck_assert_uint_eq(ctx.error_count, 0);
}

END_TEST

static Suite *negotiation_suite(void) {
	Suite *suite;
	TCase *tc;

	suite = suite_create("negotiation");
	tc = tcase_create("RFC 1143");

	tcase_add_test(tc, test_send_negotiation_table);
	tcase_add_test(tc, test_receive_initial_negotiation);
	tcase_add_test(tc, test_respond_negotiation);
	tcase_add_test(tc, test_stale_response_does_nothing);
	tcase_add_test(tc, test_withdrawn_request_makes_response_stale);

	suite_add_tcase(suite, tc);

	return suite;
}

int main(void) {
	Suite *suite;
	SRunner *runner;
	int failed;

	suite = negotiation_suite();
	runner = srunner_create(suite);

	srunner_run_all(runner, CK_VERBOSE);
	failed = srunner_ntests_failed(runner);

	srunner_free(runner);

	return failed == 0 ? 0 : 1;
}
