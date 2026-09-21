/* SPDX-License-Identifier: Zlib */
/* Copyright (c) 2026 Friedel Schön */

#include <check.h>
#include <minitelnet.h>
#include <stddef.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof *(a))

struct test_context {
	unsigned char output[256];
	size_t output_size;
	size_t send_events;
};

static void handler(struct telnet *telnet,
                    enum telnet_event_type type,
                    const union telnet_event *event,
                    void *userdata) {
	struct test_context *ctx = userdata;

	(void) telnet;

	/*
	 * The send API should only produce TELNET_EV_SEND events in these
	 * tests. Negotiation is tested separately.
	 */
	ck_assert_int_eq(type, TELNET_EV_SEND);

	ck_assert_msg(
	    ctx->output_size + event->data.size <= sizeof(ctx->output),
	    "test output buffer overflow");

	memcpy(ctx->output + ctx->output_size,
	       event->data.buffer,
	       event->data.size);

	ctx->output_size += event->data.size;
	ctx->send_events++;
}

static void init_telnet(struct telnet *telnet, struct test_context *ctx) {
	memset(ctx, 0, sizeof(*ctx));
	telnet_init(telnet, handler, ctx);
}

static void assert_output(const struct test_context *ctx,
                          const unsigned char *expected,
                          size_t expected_size) {
	ck_assert_msg(
	    ctx->output_size == expected_size,
	    "expected %lu output bytes, got %lu",
	    (unsigned long) expected_size,
	    (unsigned long) ctx->output_size);

	ck_assert_mem_eq(ctx->output, expected, expected_size);
}

/*
 * Plain application data should pass through unchanged.
 */
START_TEST(test_send_plain_data) {
	static const unsigned char input[] = {
		'h', 'e', 'l', 'l', 'o'
	};
	static const unsigned char expected[] = {
		'h', 'e', 'l', 'l', 'o'
	};

	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_data(&telnet, input, sizeof(input));

	assert_output(&ctx, expected, sizeof(expected));
}
END_TEST

/*
 * IAC occurring in application data must be escaped as IAC IAC.
 */
START_TEST(test_send_escaped_iac) {
	static const unsigned char input[] = {
		'a', TELNET_IAC, 'b'
	};
	static const unsigned char expected[] = {
		'a', TELNET_IAC, TELNET_IAC, 'b'
	};

	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_data(&telnet, input, sizeof(input));

	assert_output(&ctx, expected, sizeof(expected));
}
END_TEST

/*
 * Every IAC byte must be escaped independently.
 */
START_TEST(test_send_multiple_iac) {
	static const unsigned char input[] = {
		TELNET_IAC,
		TELNET_IAC,
		'a',
		TELNET_IAC
	};
	static const unsigned char expected[] = {
		TELNET_IAC, TELNET_IAC,
		TELNET_IAC, TELNET_IAC,
		'a',
		TELNET_IAC, TELNET_IAC
	};

	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_data(&telnet, input, sizeof(input));

	assert_output(&ctx, expected, sizeof(expected));
}
END_TEST

/*
 * Sending a Telnet command should produce IAC followed by the command.
 */
START_TEST(test_send_command) {
	static const unsigned char expected[] = {
		TELNET_IAC, TELNET_CMD_NOP
	};

	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_command(&telnet, TELNET_CMD_NOP);

	assert_output(&ctx, expected, sizeof(expected));
}
END_TEST

/*
 * A subnegotiation consists of:
 *
 *     IAC SB option payload IAC SE
 */
START_TEST(test_send_subnegotiation) {
	static const unsigned char payload[] = {
		'a', 'b', 'c'
	};
	static const unsigned char expected[] = {
		TELNET_IAC, TELNET_CMD_SB, 42,
		'a', 'b', 'c',
		TELNET_IAC, TELNET_CMD_SE
	};

	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_subnegotiation(
	    &telnet, 42, payload, sizeof(payload));

	telnet_send_subnegotiation_end(&telnet, 42);

	assert_output(&ctx, expected, sizeof(expected));
}
END_TEST

/*
 * IAC bytes inside subnegotiation data are escaped in the same manner as
 * ordinary application data.
 */
START_TEST(test_send_subnegotiation_escaped_iac) {
	static const unsigned char payload[] = {
		'a', TELNET_IAC, 'b'
	};
	static const unsigned char expected[] = {
		TELNET_IAC, TELNET_CMD_SB, 42,
		'a',
		TELNET_IAC, TELNET_IAC,
		'b',
		TELNET_IAC, TELNET_CMD_SE
	};

	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_subnegotiation(
	    &telnet, 42, payload, sizeof(payload));

	telnet_send_subnegotiation_end(&telnet, 42);

	assert_output(&ctx, expected, sizeof(expected));
}
END_TEST

/*
 * Repeated calls for the same option append to the currently open
 * subnegotiation rather than opening a new one.
 */
START_TEST(test_send_subnegotiation_streaming) {
	static const unsigned char first[] = {
		'a', 'b'
	};
	static const unsigned char second[] = {
		'c', 'd'
	};
	static const unsigned char expected[] = {
		TELNET_IAC, TELNET_CMD_SB, 42,
		'a', 'b',
		'c', 'd',
		TELNET_IAC, TELNET_CMD_SE
	};

	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_subnegotiation(
	    &telnet, 42, first, sizeof(first));

	telnet_send_subnegotiation(
	    &telnet, 42, second, sizeof(second));

	telnet_send_subnegotiation_end(&telnet, 42);

	assert_output(&ctx, expected, sizeof(expected));
}
END_TEST

/*
 * Starting a subnegotiation for another option implicitly closes the
 * currently open subnegotiation.
 */
START_TEST(test_send_subnegotiation_switch_option) {
	static const unsigned char first[] = {
		'a'
	};
	static const unsigned char second[] = {
		'b'
	};
	static const unsigned char expected[] = {
		TELNET_IAC, TELNET_CMD_SB, 42,
		'a',
		TELNET_IAC, TELNET_CMD_SE,

		TELNET_IAC, TELNET_CMD_SB, 43,
		'b',
		TELNET_IAC, TELNET_CMD_SE
	};

	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_subnegotiation(
	    &telnet, 42, first, sizeof(first));

	telnet_send_subnegotiation(
	    &telnet, 43, second, sizeof(second));

	telnet_send_subnegotiation_end(&telnet, 43);

	assert_output(&ctx, expected, sizeof(expected));
}
END_TEST

/*
 * Ordinary application data cannot occur inside a Telnet
 * subnegotiation. Sending data therefore closes an open
 * subnegotiation first.
 */
START_TEST(test_send_data_closes_subnegotiation) {
	static const unsigned char payload[] = {
		'a'
	};
	static const unsigned char data[] = {
		'b'
	};
	static const unsigned char expected[] = {
		TELNET_IAC, TELNET_CMD_SB, 42,
		'a',
		TELNET_IAC, TELNET_CMD_SE,
		'b'
	};

	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_subnegotiation(
	    &telnet, 42, payload, sizeof(payload));

	telnet_send_data(
	    &telnet, data, sizeof(data));

	assert_output(&ctx, expected, sizeof(expected));
}
END_TEST

/*
 * Ending an option other than the currently open subnegotiation must
 * have no effect.
 */
START_TEST(test_send_subnegotiation_end_wrong_option) {
	static const unsigned char payload[] = {
		'a'
	};
	static const unsigned char expected[] = {
		TELNET_IAC, TELNET_CMD_SB, 42,
		'a',
		TELNET_IAC, TELNET_CMD_SE
	};

	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_subnegotiation(
	    &telnet, 42, payload, sizeof(payload));

	/* Must not close option 42. */
	telnet_send_subnegotiation_end(&telnet, 43);

	/* This one must close it. */
	telnet_send_subnegotiation_end(&telnet, 42);

	assert_output(&ctx, expected, sizeof(expected));
}
END_TEST

/*
 * Ending a subnegotiation when none is active is a no-op.
 */
START_TEST(test_send_subnegotiation_end_without_open) {
	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_subnegotiation_end(&telnet, 42);

	ck_assert_uint_eq(ctx.output_size, 0);
	ck_assert_uint_eq(ctx.send_events, 0);
}
END_TEST

/*
 * An empty subnegotiation is valid. Starting it emits its header and
 * ending it emits IAC SE.
 */
START_TEST(test_send_empty_subnegotiation) {
	static const unsigned char expected[] = {
		TELNET_IAC, TELNET_CMD_SB, 42,
		TELNET_IAC, TELNET_CMD_SE
	};

	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_subnegotiation(&telnet, 42, NULL, 0);
	telnet_send_subnegotiation_end(&telnet, 42);

	assert_output(&ctx, expected, sizeof(expected));
}
END_TEST

/*
 * A zero-length data write must not emit anything.
 */
START_TEST(test_send_empty_data) {
	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_data(&telnet, NULL, 0);

	ck_assert_uint_eq(ctx.output_size, 0);
	ck_assert_uint_eq(ctx.send_events, 0);
}
END_TEST

/*
 * Sending zero-length application data while a subnegotiation is open
 * still constitutes switching back to normal data and must therefore
 * close the subnegotiation.
 *
 * This test encodes that API semantic explicitly. Remove it if an empty
 * send_data() is intentionally specified as a complete no-op.
 */
START_TEST(test_send_empty_data_closes_subnegotiation) {
	static const unsigned char expected[] = {
		TELNET_IAC, TELNET_CMD_SB, 42,
		'a',
		TELNET_IAC, TELNET_CMD_SE
	};
	static const unsigned char payload[] = {
		'a'
	};

	struct telnet telnet;
	struct test_context ctx;

	init_telnet(&telnet, &ctx);

	telnet_send_subnegotiation(
	    &telnet, 42, payload, sizeof(payload));

	telnet_send_data(&telnet, NULL, 0);

	assert_output(&ctx, expected, sizeof(expected));
}
END_TEST

static Suite *send_suite(void) {
	Suite *suite;
	TCase *data;
	TCase *command;
	TCase *subneg;

	suite = suite_create("send");

	data = tcase_create("data");
	tcase_add_test(data, test_send_plain_data);
	tcase_add_test(data, test_send_escaped_iac);
	tcase_add_test(data, test_send_multiple_iac);
	tcase_add_test(data, test_send_empty_data);
	tcase_add_test(data, test_send_empty_data_closes_subnegotiation);
	suite_add_tcase(suite, data);

	command = tcase_create("command");
	tcase_add_test(command, test_send_command);
	suite_add_tcase(suite, command);

	subneg = tcase_create("subnegotiation");
	tcase_add_test(subneg, test_send_subnegotiation);
	tcase_add_test(subneg, test_send_subnegotiation_escaped_iac);
	tcase_add_test(subneg, test_send_subnegotiation_streaming);
	tcase_add_test(subneg, test_send_subnegotiation_switch_option);
	tcase_add_test(subneg, test_send_data_closes_subnegotiation);
	tcase_add_test(subneg, test_send_subnegotiation_end_wrong_option);
	tcase_add_test(subneg, test_send_subnegotiation_end_without_open);
	tcase_add_test(subneg, test_send_empty_subnegotiation);
	suite_add_tcase(suite, subneg);

	return suite;
}

int main(void) {
	Suite *suite;
	SRunner *runner;
	int failed;

	suite = send_suite();
	runner = srunner_create(suite);

	srunner_run_all(runner, CK_VERBOSE);
	failed = srunner_ntests_failed(runner);

	srunner_free(runner);

	return failed == 0 ? 0 : 1;
}
