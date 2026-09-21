/* SPDX-License-Identifier: Zlib */
/* Copyright (c) 2026 Friedel Schön */

#include <check.h>
#include <minitelnet-def.h>
#include <minitelnet.h>
#include <stddef.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof *(a))

struct event_case {
	enum telnet_event_type type;
	union telnet_event data;
};

struct test_case {
	const char *name;

	const unsigned char *input;
	size_t input_size;

	const struct event_case *events;
	size_t event_count;
	size_t event_ptr;
};

static struct test_case cases[] = {
	{
	    .name = "plain data",
	    .input = (const unsigned char *) "hello world",
	    .input_size = 11,

	    .events = (const struct event_case[]){
	        {
	            TELNET_EV_DATA,
	            { .data = {
	                  (const unsigned char *) "hello world",
	                  11,
	                  0,
	              } },
	        },
	    },
	    .event_count = 1,
	},
	{
	    .name = "escape character",
	    .input = (const unsigned char[]){
	        'h',
	        'e',
	        'l',
	        'l',
	        'o',
	        0xff,
	        0xff,
	        'w',
	        'o',
	        'r',
	        'l',
	        'd',
	    },
	    .input_size = 12,

	    .events = (const struct event_case[]){
	        {
	            TELNET_EV_DATA,
	            { .data = {
	                  (const unsigned char *) "hello",
	                  5,
	                  0,
	              } },
	        },
	        {
	            TELNET_EV_DATA,
	            { .data = {
	                  (const unsigned char[]){ 0xff },
	                  1,
	                  5,
	              } },
	        },
	        {
	            TELNET_EV_DATA,
	            { .data = {
	                  (const unsigned char *) "world",
	                  5,
	                  6,
	              } },
	        },
	    },
	    .event_count = 3,
	},
	{
	    .name = "command",
	    .input = (const unsigned char[]){
	        'a',
	        TELNET_IAC,
	        TELNET_CMD_NOP,
	        'b',
	    },
	    .input_size = 4,

	    .events = (const struct event_case[]){
	        {
	            TELNET_EV_DATA,
	            { .data = {
	                  (const unsigned char *) "a",
	                  1,
	                  0,
	              } },
	        },
	        {
	            TELNET_EV_COMMAND,
	            { .command = TELNET_CMD_NOP },
	        },
	        {
	            TELNET_EV_DATA,
	            { .data = {
	                  (const unsigned char *) "b",
	                  1,
	                  1,
	              } },
	        },
	    },
	    .event_count = 3,
	},
	{
	    .name = "subnegotiation",
	    .input = (const unsigned char[]){
	        TELNET_IAC,
	        TELNET_CMD_SB,
	        TELNET_OPT_TTYPE,
	        TELNET_TTYPE_SEND,
	        TELNET_IAC,
	        TELNET_CMD_SE,
	    },
	    .input_size = 6,

	    .events = (const struct event_case[]){
	        {
	            TELNET_EV_SUBNEG,
	            { .subneg = {
	                  (const unsigned char[]){
	                      TELNET_TTYPE_SEND,
	                  },
	                  1,
	                  0,
	                  TELNET_OPT_TTYPE,
	              } },
	        },
	    },
	    .event_count = 1,
	},
	{
	    .name = "escaped IAC in subnegotiation",
	    .input = (const unsigned char[]){
	        TELNET_IAC,
	        TELNET_CMD_SB,
	        42,
	        'a',
	        TELNET_IAC,
	        TELNET_IAC,
	        'b',
	        TELNET_IAC,
	        TELNET_CMD_SE,
	    },
	    .input_size = 9,

	    .events = (const struct event_case[]){
	        {
	            TELNET_EV_SUBNEG,
	            { .subneg = {
	                  (const unsigned char *) "a",
	                  1,
	                  0,
	                  42,
	              } },
	        },
	        {
	            TELNET_EV_SUBNEG,
	            { .subneg = {
	                  (const unsigned char[]){ TELNET_IAC },
	                  1,
	                  1,
	                  42,
	              } },
	        },
	        {
	            TELNET_EV_SUBNEG,
	            { .subneg = {
	                  (const unsigned char *) "b",
	                  1,
	                  2,
	                  42,
	              } },
	        },
	    },
	    .event_count = 3,
	},
	{
	    .name = "loose SE",
	    .input = (const unsigned char[]){
	        TELNET_IAC,
	        TELNET_CMD_SE,
	    },
	    .input_size = 2,

	    .events = (const struct event_case[]){
	        {
	            TELNET_EV_ERROR,
	            { .error = TELNET_ERR_INVALID_SE },
	        },
	    },
	    .event_count = 1,
	},
};

static void handler(struct telnet *telnet,
                    enum telnet_event_type type,
                    const union telnet_event *event,
                    void *userdata) {
	struct test_case *test = userdata;
	const struct event_case *expected_case;
	const union telnet_event *expected;

	(void) telnet;

	ck_assert_msg(
	    test->event_ptr < test->event_count,
	    "%s: unexpected event %d",
	    test->name,
	    (int) type);

	expected_case = &test->events[test->event_ptr];
	expected = &expected_case->data;

	ck_assert_msg(
	    expected_case->type == type,
	    "%s: event %lu: expected type %d, got %d",
	    test->name,
	    (unsigned long) test->event_ptr,
	    (int) expected_case->type,
	    (int) type);

	switch (type) {
		case TELNET_EV_COMMAND:
			ck_assert_msg(
			    event->command == expected->command,
			    "%s: event %lu: expected command %d, got %d",
			    test->name,
			    (unsigned long) test->event_ptr,
			    (int) expected->command,
			    (int) event->command);
			break;

		case TELNET_EV_SEND:
		case TELNET_EV_DATA:
			ck_assert_msg(
			    event->data.size == expected->data.size,
			    "%s: event %lu: expected size %lu, got %lu",
			    test->name,
			    (unsigned long) test->event_ptr,
			    (unsigned long) expected->data.size,
			    (unsigned long) event->data.size);

			ck_assert_msg(
			    event->data.offset == expected->data.offset,
			    "%s: event %lu: expected offset %lu, got %lu",
			    test->name,
			    (unsigned long) test->event_ptr,
			    (unsigned long) expected->data.offset,
			    (unsigned long) event->data.offset);

			ck_assert_mem_eq(
			    event->data.buffer,
			    expected->data.buffer,
			    event->data.size);
			break;

		case TELNET_EV_SUBNEG:
			ck_assert_msg(
			    event->subneg.size == expected->subneg.size,
			    "%s: event %lu: expected size %lu, got %lu",
			    test->name,
			    (unsigned long) test->event_ptr,
			    (unsigned long) expected->subneg.size,
			    (unsigned long) event->subneg.size);

			ck_assert_msg(
			    event->subneg.offset == expected->subneg.offset,
			    "%s: event %lu: expected offset %lu, got %lu",
			    test->name,
			    (unsigned long) test->event_ptr,
			    (unsigned long) expected->subneg.offset,
			    (unsigned long) event->subneg.offset);

			ck_assert_msg(
			    event->subneg.option == expected->subneg.option,
			    "%s: event %lu: expected option %u, got %u",
			    test->name,
			    (unsigned long) test->event_ptr,
			    (unsigned int) expected->subneg.option,
			    (unsigned int) event->subneg.option);

			ck_assert_mem_eq(
			    event->subneg.buffer,
			    expected->subneg.buffer,
			    event->subneg.size);
			break;

		case TELNET_EV_ERROR:
			ck_assert_msg(
			    event->error == expected->error,
			    "%s: event %lu: expected error %d, got %d",
			    test->name,
			    (unsigned long) test->event_ptr,
			    (int) expected->error,
			    (int) event->error);
			break;

		case TELNET_EV_NEG:
			/*
			 * Negotiation state transitions are tested by
			 * test_negotiation.c.
			 */
			break;
	}

	test->event_ptr++;
}

START_TEST(test_parser) {
	size_t i;

	for (i = 0; i < ARRAY_SIZE(cases); ++i) {
		struct telnet telnet;
		struct test_case *test = &cases[i];

		test->event_ptr = 0;

		telnet_init(&telnet, handler, test);
		telnet_feed(&telnet, test->input, test->input_size);

		ck_assert_msg(
		    test->event_ptr == test->event_count,
		    "%s: expected %lu events, got %lu",
		    test->name,
		    (unsigned long) test->event_count,
		    (unsigned long) test->event_ptr);
	}
}
END_TEST

/*
 * Parser state must be retained between calls to telnet_feed().
 *
 * Feed an escaped IAC one byte at a time to ensure that splitting an IAC
 * sequence across input buffers produces the same result.
 */
START_TEST(test_parser_split_input) {
	const struct event_case events[] = {
		{
		    TELNET_EV_DATA,
		    { .data = {
		          (const unsigned char *) "a",
		          1,
		          0,
		      } },
		},
		{
		    TELNET_EV_DATA,
		    { .data = {
		          (const unsigned char[]){ TELNET_IAC },
		          1,
		          1,
		      } },
		},
		{
		    TELNET_EV_DATA,
		    { .data = {
		          (const unsigned char *) "b",
		          1,
		          2,
		      } },
		},
	};

	struct test_case test = {
		.name = "split input",
		.events = events,
		.event_count = ARRAY_SIZE(events),
		.event_ptr = 0,
	};

	static const unsigned char input[] = {
		'a',
		TELNET_IAC,
		TELNET_IAC,
		'b',
	};

	struct telnet telnet;
	size_t i;

	telnet_init(&telnet, handler, &test);

	for (i = 0; i < ARRAY_SIZE(input); ++i)
		telnet_feed(&telnet, &input[i], 1);

	ck_assert_uint_eq(test.event_ptr, test.event_count);
}
END_TEST

static Suite *parser_suite(void) {
	Suite *suite;
	TCase *tc;

	suite = suite_create("parser");
	tc = tcase_create("parser");

	tcase_add_test(tc, test_parser);
	tcase_add_test(tc, test_parser_split_input);

	suite_add_tcase(suite, tc);

	return suite;
}

int main(void) {
	Suite *suite;
	SRunner *runner;
	int failed;

	suite = parser_suite();
	runner = srunner_create(suite);

	srunner_run_all(runner, CK_VERBOSE);
	failed = srunner_ntests_failed(runner);

	srunner_free(runner);

	return failed == 0 ? 0 : 1;
}
