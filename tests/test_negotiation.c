/* SPDX-License-Identifier: Zlib */
/* Copyright (c) 2026 Friedel Schön */

#include <check.h>
#include <minitelnet-negotiation.h>
#include <stddef.h>
#include <string.h>

#define OPTION 42

#define ARRAY_SIZE(a) (sizeof(a) / sizeof *(a))

/*
 * Test-only state injection.
 *
 * Keep knowledge of the nibble representation confined to these helpers.
 * Assertions use the public accessors.
 */
static void set_local(telnet_negotiation_t neg,
                      unsigned char option,
                      enum telnet_negotiation_state state) {
	neg[option] =
	    (unsigned char) ((neg[option] & 0xf0) |
	                     ((unsigned char) state & 0x0f));
}

static void set_peer(telnet_negotiation_t neg,
                     unsigned char option,
                     enum telnet_negotiation_state state) {
	neg[option] =
	    (unsigned char) ((neg[option] & 0x0f) |
	                     (((unsigned char) state & 0x0f) << 4));
}

static void set_state(telnet_negotiation_t neg,
                      unsigned char option,
                      int local,
                      enum telnet_negotiation_state state) {
	if (local)
		set_local(neg, option, state);
	else
		set_peer(neg, option, state);
}

static enum telnet_negotiation_state
get_state(const telnet_negotiation_t neg,
          unsigned char option,
          int local) {
	if (local)
		return telnet_negotiation_local(neg, option);

	return telnet_negotiation_peer(neg, option);
}

static void assert_transition(
    const char *name,
    const struct telnet_negotiation_transition *trns,
    int local,
    enum telnet_negotiation_state state,
    enum telnet_command outgoing,
    int error) {
	ck_assert_msg(
	    trns->option == OPTION,
	    "%s: expected option %u, got %u",
	    name,
	    OPTION,
	    (unsigned int) trns->option);

	ck_assert_msg(
	    trns->local == local,
	    "%s: expected local=%d, got %d",
	    name,
	    local,
	    trns->local);

	ck_assert_msg(
	    trns->state == state,
	    "%s: expected state %d, got %d",
	    name,
	    (int) state,
	    (int) trns->state);

	ck_assert_msg(
	    trns->outgoing == outgoing,
	    "%s: expected outgoing %d, got %d",
	    name,
	    (int) outgoing,
	    (int) trns->outgoing);

	ck_assert_msg(
	    !!trns->error == !!error,
	    "%s: expected error=%d, got %d",
	    name,
	    !!error,
	    !!trns->error);
}

/*
 * Application-initiated negotiation.
 */

struct send_case {
	const char *name;

	enum telnet_command command;
	int local;

	enum telnet_negotiation_state initial;
	enum telnet_negotiation_state expected;

	int updated;
	enum telnet_command outgoing;
};

static const struct send_case send_cases[] = {
    /* DO -- enable peer option */
    {
        "DO: NO -> WANTYES",
        TELNET_CMD_DO,
        0,
        TELNET_OPTION_NO,
        TELNET_OPTION_WANTYES,
        1,
        TELNET_CMD_DO,
    },
    {
        "DO: YES -> YES",
        TELNET_CMD_DO,
        0,
        TELNET_OPTION_YES,
        TELNET_OPTION_YES,
        0,
        0,
    },
    {
        "DO: WANTNO -> WANTNO_OPPOSITE",
        TELNET_CMD_DO,
        0,
        TELNET_OPTION_WANTNO,
        TELNET_OPTION_WANTNO_OPPOSITE,
        1,
        0,
    },
    {
        "DO: WANTYES -> WANTYES",
        TELNET_CMD_DO,
        0,
        TELNET_OPTION_WANTYES,
        TELNET_OPTION_WANTYES,
        0,
        0,
    },
    {
        "DO: WANTNO_OPPOSITE -> WANTNO_OPPOSITE",
        TELNET_CMD_DO,
        0,
        TELNET_OPTION_WANTNO_OPPOSITE,
        TELNET_OPTION_WANTNO_OPPOSITE,
        0,
        0,
    },
    {
        "DO: WANTYES_OPPOSITE -> WANTYES",
        TELNET_CMD_DO,
        0,
        TELNET_OPTION_WANTYES_OPPOSITE,
        TELNET_OPTION_WANTYES,
        1,
        0,
    },
    {
        "DO: REQUEST_PENDING -> REQUEST_PENDING",
        TELNET_CMD_DO,
        0,
        TELNET_OPTION_REQUEST_PENDING,
        TELNET_OPTION_REQUEST_PENDING,
        0,
        0,
    },

    /* DONT -- disable peer option */
    {
        "DONT: NO -> NO",
        TELNET_CMD_DONT,
        0,
        TELNET_OPTION_NO,
        TELNET_OPTION_NO,
        0,
        0,
    },
    {
        "DONT: YES -> WANTNO",
        TELNET_CMD_DONT,
        0,
        TELNET_OPTION_YES,
        TELNET_OPTION_WANTNO,
        1,
        TELNET_CMD_DONT,
    },
    {
        "DONT: WANTNO -> WANTNO",
        TELNET_CMD_DONT,
        0,
        TELNET_OPTION_WANTNO,
        TELNET_OPTION_WANTNO,
        0,
        0,
    },
    {
        "DONT: WANTYES -> WANTYES_OPPOSITE",
        TELNET_CMD_DONT,
        0,
        TELNET_OPTION_WANTYES,
        TELNET_OPTION_WANTYES_OPPOSITE,
        1,
        0,
    },
    {
        "DONT: WANTNO_OPPOSITE -> WANTNO",
        TELNET_CMD_DONT,
        0,
        TELNET_OPTION_WANTNO_OPPOSITE,
        TELNET_OPTION_WANTNO,
        1,
        0,
    },
    {
        "DONT: WANTYES_OPPOSITE -> WANTYES_OPPOSITE",
        TELNET_CMD_DONT,
        0,
        TELNET_OPTION_WANTYES_OPPOSITE,
        TELNET_OPTION_WANTYES_OPPOSITE,
        0,
        0,
    },
    {
        "DONT: REQUEST_PENDING -> REQUEST_PENDING",
        TELNET_CMD_DONT,
        0,
        TELNET_OPTION_REQUEST_PENDING,
        TELNET_OPTION_REQUEST_PENDING,
        0,
        0,
    },

    /* WILL -- enable local option */
    {
        "WILL: NO -> WANTYES",
        TELNET_CMD_WILL,
        1,
        TELNET_OPTION_NO,
        TELNET_OPTION_WANTYES,
        1,
        TELNET_CMD_WILL,
    },
    {
        "WILL: YES -> YES",
        TELNET_CMD_WILL,
        1,
        TELNET_OPTION_YES,
        TELNET_OPTION_YES,
        0,
        0,
    },
    {
        "WILL: WANTNO -> WANTNO_OPPOSITE",
        TELNET_CMD_WILL,
        1,
        TELNET_OPTION_WANTNO,
        TELNET_OPTION_WANTNO_OPPOSITE,
        1,
        0,
    },
    {
        "WILL: WANTYES -> WANTYES",
        TELNET_CMD_WILL,
        1,
        TELNET_OPTION_WANTYES,
        TELNET_OPTION_WANTYES,
        0,
        0,
    },
    {
        "WILL: WANTNO_OPPOSITE -> WANTNO_OPPOSITE",
        TELNET_CMD_WILL,
        1,
        TELNET_OPTION_WANTNO_OPPOSITE,
        TELNET_OPTION_WANTNO_OPPOSITE,
        0,
        0,
    },
    {
        "WILL: WANTYES_OPPOSITE -> WANTYES",
        TELNET_CMD_WILL,
        1,
        TELNET_OPTION_WANTYES_OPPOSITE,
        TELNET_OPTION_WANTYES,
        1,
        0,
    },
    {
        "WILL: REQUEST_PENDING -> REQUEST_PENDING",
        TELNET_CMD_WILL,
        1,
        TELNET_OPTION_REQUEST_PENDING,
        TELNET_OPTION_REQUEST_PENDING,
        0,
        0,
    },

    /* WONT -- disable local option */
    {
        "WONT: NO -> NO",
        TELNET_CMD_WONT,
        1,
        TELNET_OPTION_NO,
        TELNET_OPTION_NO,
        0,
        0,
    },
    {
        "WONT: YES -> WANTNO",
        TELNET_CMD_WONT,
        1,
        TELNET_OPTION_YES,
        TELNET_OPTION_WANTNO,
        1,
        TELNET_CMD_WONT,
    },
    {
        "WONT: WANTNO -> WANTNO",
        TELNET_CMD_WONT,
        1,
        TELNET_OPTION_WANTNO,
        TELNET_OPTION_WANTNO,
        0,
        0,
    },
    {
        "WONT: WANTYES -> WANTYES_OPPOSITE",
        TELNET_CMD_WONT,
        1,
        TELNET_OPTION_WANTYES,
        TELNET_OPTION_WANTYES_OPPOSITE,
        1,
        0,
    },
    {
        "WONT: WANTNO_OPPOSITE -> WANTNO",
        TELNET_CMD_WONT,
        1,
        TELNET_OPTION_WANTNO_OPPOSITE,
        TELNET_OPTION_WANTNO,
        1,
        0,
    },
    {
        "WONT: WANTYES_OPPOSITE -> WANTYES_OPPOSITE",
        TELNET_CMD_WONT,
        1,
        TELNET_OPTION_WANTYES_OPPOSITE,
        TELNET_OPTION_WANTYES_OPPOSITE,
        0,
        0,
    },
    {
        "WONT: REQUEST_PENDING -> REQUEST_PENDING",
        TELNET_CMD_WONT,
        1,
        TELNET_OPTION_REQUEST_PENDING,
        TELNET_OPTION_REQUEST_PENDING,
        0,
        0,
    },
};

START_TEST(test_send_table) {
	size_t i;

	for (i = 0; i < ARRAY_SIZE(send_cases); ++i) {
		const struct send_case *tc = &send_cases[i];
		telnet_negotiation_t neg = {0};
		struct telnet_negotiation_transition trns;
		enum telnet_negotiation_state actual;
		int updated;

		memset(&trns, 0, sizeof(trns));
		set_state(neg, OPTION, tc->local, tc->initial);

		updated = telnet_negotiation_send(
		    neg, tc->command, OPTION, &trns);

		ck_assert_msg(
		    !!updated == !!tc->updated,
		    "%s: expected updated=%d, got %d",
		    tc->name,
		    tc->updated,
		    updated);

		actual = get_state(neg, OPTION, tc->local);

		ck_assert_msg(
		    actual == tc->expected,
		    "%s: expected state %d, got %d",
		    tc->name,
		    (int) tc->expected,
		    (int) actual);

		if (updated)
			assert_transition(
			    tc->name,
			    &trns,
			    tc->local,
			    tc->expected,
			    tc->outgoing,
			    0);
	}
}
END_TEST

/*
 * Incoming negotiation.
 */

struct feed_case {
	const char *name;

	enum telnet_command command;
	int local;

	enum telnet_negotiation_state initial;
	enum telnet_negotiation_state expected;

	int updated;
	enum telnet_command outgoing;
	int error;
};

static const struct feed_case feed_cases[] = {
    /* WILL -- peer option */
    {
        "WILL: NO -> REQUEST_PENDING",
        TELNET_CMD_WILL,
        0,
        TELNET_OPTION_NO,
        TELNET_OPTION_REQUEST_PENDING,
        1,
        0,
        0,
    },
    {
        "WILL: YES -> YES",
        TELNET_CMD_WILL,
        0,
        TELNET_OPTION_YES,
        TELNET_OPTION_YES,
        0,
        0,
        0,
    },
    {
        "WILL: WANTYES -> YES",
        TELNET_CMD_WILL,
        0,
        TELNET_OPTION_WANTYES,
        TELNET_OPTION_YES,
        1,
        0,
        0,
    },
    {
        "WILL: WANTNO -> NO",
        TELNET_CMD_WILL,
        0,
        TELNET_OPTION_WANTNO,
        TELNET_OPTION_NO,
        1,
        0,
        1,
    },
    {
        "WILL: WANTYES_OPPOSITE -> WANTNO",
        TELNET_CMD_WILL,
        0,
        TELNET_OPTION_WANTYES_OPPOSITE,
        TELNET_OPTION_WANTNO,
        1,
        TELNET_CMD_DONT,
        0,
    },
    {
        "WILL: WANTNO_OPPOSITE -> YES",
        TELNET_CMD_WILL,
        0,
        TELNET_OPTION_WANTNO_OPPOSITE,
        TELNET_OPTION_YES,
        1,
        0,
        1,
    },
    {
        "WILL: REQUEST_PENDING -> REQUEST_PENDING",
        TELNET_CMD_WILL,
        0,
        TELNET_OPTION_REQUEST_PENDING,
        TELNET_OPTION_REQUEST_PENDING,
        0,
        0,
        0,
    },

    /* WONT -- peer option */
    {
        "WONT: NO -> NO",
        TELNET_CMD_WONT,
        0,
        TELNET_OPTION_NO,
        TELNET_OPTION_NO,
        0,
        0,
        0,
    },
    {
        "WONT: YES -> NO",
        TELNET_CMD_WONT,
        0,
        TELNET_OPTION_YES,
        TELNET_OPTION_NO,
        1,
        TELNET_CMD_DONT,
        0,
    },
    {
        "WONT: WANTYES -> NO",
        TELNET_CMD_WONT,
        0,
        TELNET_OPTION_WANTYES,
        TELNET_OPTION_NO,
        1,
        0,
        0,
    },
    {
        "WONT: WANTNO -> NO",
        TELNET_CMD_WONT,
        0,
        TELNET_OPTION_WANTNO,
        TELNET_OPTION_NO,
        1,
        0,
        0,
    },
    {
        "WONT: WANTYES_OPPOSITE -> NO",
        TELNET_CMD_WONT,
        0,
        TELNET_OPTION_WANTYES_OPPOSITE,
        TELNET_OPTION_NO,
        1,
        0,
        0,
    },
    {
        "WONT: WANTNO_OPPOSITE -> WANTYES",
        TELNET_CMD_WONT,
        0,
        TELNET_OPTION_WANTNO_OPPOSITE,
        TELNET_OPTION_WANTYES,
        1,
        TELNET_CMD_DO,
        0,
    },
    {
        "WONT: REQUEST_PENDING -> NO",
        TELNET_CMD_WONT,
        0,
        TELNET_OPTION_REQUEST_PENDING,
        TELNET_OPTION_NO,
        1,
        0,
        0,
    },

    /* DO -- local option */
    {
        "DO: NO -> REQUEST_PENDING",
        TELNET_CMD_DO,
        1,
        TELNET_OPTION_NO,
        TELNET_OPTION_REQUEST_PENDING,
        1,
        0,
        0,
    },
    {
        "DO: YES -> YES",
        TELNET_CMD_DO,
        1,
        TELNET_OPTION_YES,
        TELNET_OPTION_YES,
        0,
        0,
        0,
    },
    {
        "DO: WANTYES -> YES",
        TELNET_CMD_DO,
        1,
        TELNET_OPTION_WANTYES,
        TELNET_OPTION_YES,
        1,
        0,
        0,
    },
    {
        "DO: WANTNO -> NO",
        TELNET_CMD_DO,
        1,
        TELNET_OPTION_WANTNO,
        TELNET_OPTION_NO,
        1,
        0,
        1,
    },
    {
        "DO: WANTYES_OPPOSITE -> WANTNO",
        TELNET_CMD_DO,
        1,
        TELNET_OPTION_WANTYES_OPPOSITE,
        TELNET_OPTION_WANTNO,
        1,
        TELNET_CMD_WONT,
        0,
    },
    {
        "DO: WANTNO_OPPOSITE -> YES",
        TELNET_CMD_DO,
        1,
        TELNET_OPTION_WANTNO_OPPOSITE,
        TELNET_OPTION_YES,
        1,
        0,
        1,
    },
    {
        "DO: REQUEST_PENDING -> REQUEST_PENDING",
        TELNET_CMD_DO,
        1,
        TELNET_OPTION_REQUEST_PENDING,
        TELNET_OPTION_REQUEST_PENDING,
        0,
        0,
        0,
    },

    /* DONT -- local option */
    {
        "DONT: NO -> NO",
        TELNET_CMD_DONT,
        1,
        TELNET_OPTION_NO,
        TELNET_OPTION_NO,
        0,
        0,
        0,
    },
    {
        "DONT: YES -> NO",
        TELNET_CMD_DONT,
        1,
        TELNET_OPTION_YES,
        TELNET_OPTION_NO,
        1,
        TELNET_CMD_WONT,
        0,
    },
    {
        "DONT: WANTYES -> NO",
        TELNET_CMD_DONT,
        1,
        TELNET_OPTION_WANTYES,
        TELNET_OPTION_NO,
        1,
        0,
        0,
    },
    {
        "DONT: WANTNO -> NO",
        TELNET_CMD_DONT,
        1,
        TELNET_OPTION_WANTNO,
        TELNET_OPTION_NO,
        1,
        0,
        0,
    },
    {
        "DONT: WANTYES_OPPOSITE -> NO",
        TELNET_CMD_DONT,
        1,
        TELNET_OPTION_WANTYES_OPPOSITE,
        TELNET_OPTION_NO,
        1,
        0,
        0,
    },
    {
        "DONT: WANTNO_OPPOSITE -> WANTYES",
        TELNET_CMD_DONT,
        1,
        TELNET_OPTION_WANTNO_OPPOSITE,
        TELNET_OPTION_WANTYES,
        1,
        TELNET_CMD_WILL,
        0,
    },
    {
        "DONT: REQUEST_PENDING -> NO",
        TELNET_CMD_DONT,
        1,
        TELNET_OPTION_REQUEST_PENDING,
        TELNET_OPTION_NO,
        1,
        0,
        0,
    },
};

START_TEST(test_feed_table) {
	size_t i;

	for (i = 0; i < ARRAY_SIZE(feed_cases); ++i) {
		const struct feed_case *tc = &feed_cases[i];
		telnet_negotiation_t neg = {0};
		struct telnet_negotiation_transition trns;
		enum telnet_negotiation_state actual;
		int updated;

		memset(&trns, 0, sizeof(trns));
		set_state(neg, OPTION, tc->local, tc->initial);

		updated = telnet_negotiation_feed(
		    neg, tc->command, OPTION, &trns);

		ck_assert_msg(
		    !!updated == !!tc->updated,
		    "%s: expected updated=%d, got %d",
		    tc->name,
		    tc->updated,
		    updated);

		actual = get_state(neg, OPTION, tc->local);

		ck_assert_msg(
		    actual == tc->expected,
		    "%s: expected state %d, got %d",
		    tc->name,
		    (int) tc->expected,
		    (int) actual);

		if (updated)
			assert_transition(
			    tc->name,
			    &trns,
			    tc->local,
			    tc->expected,
			    tc->outgoing,
			    tc->error);
	}
}
END_TEST

/*
 * Application response to REQUEST_PENDING.
 */

struct respond_case {
	const char *name;
	enum telnet_command command;
	int local;
	enum telnet_negotiation_state expected;
};

static const struct respond_case respond_cases[] = {
    {
        "accept DO with WILL",
        TELNET_CMD_WILL,
        1,
        TELNET_OPTION_YES,
    },
    {
        "reject DO with WONT",
        TELNET_CMD_WONT,
        1,
        TELNET_OPTION_NO,
    },
    {
        "accept WILL with DO",
        TELNET_CMD_DO,
        0,
        TELNET_OPTION_YES,
    },
    {
        "reject WILL with DONT",
        TELNET_CMD_DONT,
        0,
        TELNET_OPTION_NO,
    },
};

START_TEST(test_respond) {
	size_t i;

	for (i = 0; i < ARRAY_SIZE(respond_cases); ++i) {
		const struct respond_case *tc = &respond_cases[i];
		telnet_negotiation_t neg = {0};
		struct telnet_negotiation_transition trns;
		int updated;

		memset(&trns, 0, sizeof(trns));

		set_state(
		    neg,
		    OPTION,
		    tc->local,
		    TELNET_OPTION_REQUEST_PENDING);

		updated = telnet_negotiation_respond(
		    neg, tc->command, OPTION, &trns);

		ck_assert_msg(
		    updated,
		    "%s: expected state transition",
		    tc->name);

		ck_assert_int_eq(
		    get_state(neg, OPTION, tc->local),
		    tc->expected);

		assert_transition(
		    tc->name,
		    &trns,
		    tc->local,
		    tc->expected,
		    tc->command,
		    0);
	}
}
END_TEST

/*
 * A response without a matching REQUEST_PENDING is stale and must not
 * initiate a negotiation.
 */
START_TEST(test_stale_response_does_nothing) {
	static const enum telnet_command commands[] = {
	    TELNET_CMD_WILL,
	    TELNET_CMD_WONT,
	    TELNET_CMD_DO,
	    TELNET_CMD_DONT,
	};

	size_t i;

	for (i = 0; i < ARRAY_SIZE(commands); ++i) {
		telnet_negotiation_t neg = {0};
		struct telnet_negotiation_transition trns;
		int local;
		int updated;

		memset(&trns, 0, sizeof(trns));

		local = commands[i] == TELNET_CMD_WILL ||
		        commands[i] == TELNET_CMD_WONT;

		updated = telnet_negotiation_respond(
		    neg, commands[i], OPTION, &trns);

		ck_assert_int_eq(updated, 0);
		ck_assert_int_eq(
		    get_state(neg, OPTION, local),
		    TELNET_OPTION_NO);
	}
}
END_TEST

/*
 * A peer may withdraw an unsolicited enable request before the application
 * has answered it. A later application response is then stale.
 */
START_TEST(test_withdrawn_request_makes_response_stale) {
	telnet_negotiation_t neg = {0};
	struct telnet_negotiation_transition trns;
	int updated;

	memset(&trns, 0, sizeof(trns));

	updated = telnet_negotiation_feed(
	    neg, TELNET_CMD_WILL, OPTION, &trns);

	ck_assert_int_eq(updated, 1);
	ck_assert_int_eq(
	    telnet_negotiation_peer(neg, OPTION),
	    TELNET_OPTION_REQUEST_PENDING);

	memset(&trns, 0, sizeof(trns));

	updated = telnet_negotiation_feed(
	    neg, TELNET_CMD_WONT, OPTION, &trns);

	ck_assert_int_eq(updated, 1);
	ck_assert_int_eq(
	    telnet_negotiation_peer(neg, OPTION),
	    TELNET_OPTION_NO);

	memset(&trns, 0, sizeof(trns));

	updated = telnet_negotiation_respond(
	    neg, TELNET_CMD_DO, OPTION, &trns);

	ck_assert_int_eq(updated, 0);
	ck_assert_int_eq(
	    telnet_negotiation_peer(neg, OPTION),
	    TELNET_OPTION_NO);
}
END_TEST

/*
 * Local and peer state for the same option are independent.
 */
START_TEST(test_directions_are_independent) {
	telnet_negotiation_t neg = {0};
	struct telnet_negotiation_transition trns;

	memset(&trns, 0, sizeof(trns));

	ck_assert_int_eq(
	    telnet_negotiation_send(
	        neg, TELNET_CMD_WILL, OPTION, &trns),
	    1);

	ck_assert_int_eq(
	    telnet_negotiation_local(neg, OPTION),
	    TELNET_OPTION_WANTYES);

	ck_assert_int_eq(
	    telnet_negotiation_peer(neg, OPTION),
	    TELNET_OPTION_NO);

	memset(&trns, 0, sizeof(trns));

	ck_assert_int_eq(
	    telnet_negotiation_feed(
	        neg, TELNET_CMD_WILL, OPTION, &trns),
	    1);

	ck_assert_int_eq(
	    telnet_negotiation_local(neg, OPTION),
	    TELNET_OPTION_WANTYES);

	ck_assert_int_eq(
	    telnet_negotiation_peer(neg, OPTION),
	    TELNET_OPTION_REQUEST_PENDING);
}
END_TEST

static Suite *negotiation_suite(void) {
	Suite *suite;
	TCase *tc;

	suite = suite_create("negotiation");
	tc = tcase_create("RFC 1143");

	tcase_add_test(tc, test_send_table);
	tcase_add_test(tc, test_feed_table);
	tcase_add_test(tc, test_respond);
	tcase_add_test(tc, test_stale_response_does_nothing);
	tcase_add_test(tc, test_withdrawn_request_makes_response_stale);
	tcase_add_test(tc, test_directions_are_independent);

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
