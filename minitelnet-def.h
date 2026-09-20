#pragma once

enum telnet_option_code {
	TELNET_OPT_BINARY = 0,                    // Binary Transmission [RFC856]
	TELNET_OPT_ECHO = 1,                      // Echo [RFC857]
	TELNET_OPT_RECONNECTION = 2,              // Reconnection [NIC 15391, 1973]
	TELNET_OPT_SUPPRESS_GO_AHEAD = 3,         // Suppress Go Ahead [RFC858]
	TELNET_OPT_AMSN = 4,                      // Approx Message Size Negotiation [NIC 15393, 1973]
	TELNET_OPT_STATUS = 5,                    // Status [RFC859]
	TELNET_OPT_TIMING_MARK = 6,               // Timing Mark [RFC860]
	TELNET_OPT_RCTE = 7,                      // Remote Controlled Trans and Echo [RFC726]
	TELNET_OPT_OUTPUT_LINE_WIDTH = 8,         // Output Line Width
	TELNET_OPT_OUTPUT_PAGE_SIZE = 9,          // Output Page Size
	TELNET_OPT_OUTPUT_CR_DISPOSITION = 10,    // Output Carriage-Return Disposition [RFC652]
	TELNET_OPT_OUTPUT_HT_STOPS = 11,          // Output Horizontal Tab Stops [RFC653]
	TELNET_OPT_OUTPUT_HT_DISPOSITION = 12,    // Output Horizontal Tab Disposition [RFC654]
	TELNET_OPT_OUTPUT_FF_DISPOSITION = 13,    // Output Formfeed Disposition [RFC655]
	TELNET_OPT_OUTPUT_VT_STOPS = 14,          // Output Vertical Tabstops [RFC656]
	TELNET_OPT_OUTPUT_VT_DISPOSITION = 15,    // Output Vertical Tab Disposition [RFC657]
	TELNET_OPT_OUTPUT_LF_DISPOSITION = 16,    // Output Linefeed Disposition [RFC658]
	TELNET_OPT_EXTENDED_ASCII = 17,           // Extended ASCII [RFC698]
	TELNET_OPT_LOGOUT = 18,                   // Logout [RFC727]
	TELNET_OPT_BYTE_MACRO = 19,               // Byte Macro [RFC735]
	TELNET_OPT_DET = 20,                      // Data Entry Terminal [RFC1043][RFC732]
	TELNET_OPT_SUPDUP = 21,                   // SUPDUP [RFC736][RFC734]
	TELNET_OPT_SUPDUP_OUTPUT = 22,            // SUPDUP Output [RFC749]
	TELNET_OPT_SEND_LOCATION = 23,            // Send Location [RFC779]
	TELNET_OPT_TTYPE = 24,                    // Terminal Type [RFC1091]
	TELNET_OPT_EOR = 25,                      // End of Record [RFC885]
	TELNET_OPT_TUID = 26,                     // TACACS User Identification [RFC927]
	TELNET_OPT_OUTMRK = 27,                   // Output Marking [RFC933]
	TELNET_OPT_TTYLOC = 28,                   // Terminal Location Number [RFC946]
	TELNET_OPT_3270_REGIME = 29,              // Telnet 3270 Regime [RFC1041]
	TELNET_OPT_X3_PAD = 30,                   // X.3 PAD [RFC1053]
	TELNET_OPT_NAWS = 31,                     // Negotiate About Window Size [RFC1073]
	TELNET_OPT_TSPEED = 32,                   // Terminal Speed [RFC1079]
	TELNET_OPT_LFLOW = 33,                    // Remote Flow Control [RFC1372]
	TELNET_OPT_LINEMODE = 34,                 // Linemode [RFC1184]
	TELNET_OPT_XDISPLOC = 35,                 // X Display Location [RFC1096]
	TELNET_OPT_OLD_ENVIRON = 36,              // Environment Option [RFC1408]
	TELNET_OPT_AUTHENTICATION = 37,           // Authentication Option [RFC2941]
	TELNET_OPT_ENCRYPT = 38,                  // Encryption Option [RFC2946]
	TELNET_OPT_NEW_ENVIRON = 39,              // New Environment Option [RFC1572]
	TELNET_OPT_TN3270E = 40,                  // TN3270E [RFC2355]
	TELNET_OPT_XAUTH = 41,                    // XAUTH
	TELNET_OPT_CHARSET = 42,                  // CHARSET
	TELNET_OPT_RSP = 43,                      // Telnet Remote Serial Port
	TELNET_OPT_COM_PORT = 44,                 // Com Port Control Option [RFC2217]
	TELNET_OPT_SUPPRESS_LOCAL_ECHO = 45,      // Telnet Suppress Local Echo
	TELNET_OPT_START_TLS = 46,                // Telnet Start TLS
	TELNET_OPT_KERMIT = 47,                   // KERMIT [RFC2840]
	TELNET_OPT_SEND_URL = 48,                 // SEND-URL
	TELNET_OPT_FORWARD_X = 49,                // FORWARD_X

	TELNET_OPT_PRAGMA_LOGON = 138,
	TELNET_OPT_SSPI_LOGON = 139,
	TELNET_OPT_PRAGMA_HEARTBEAT = 140,

	TELNET_OPT_EXTENDED_OPTIONS = 255,    // Extended-Options-List [RFC861]
};

enum telnet_ttype_command {
	TELNET_TTYPE_IS = 0,
	TELNET_TTYPE_SEND = 1,
};

enum telnet_status_command {
	TELNET_STATUS_IS = 0,
	TELNET_STATUS_SEND = 1,
};

enum telnet_new_environ_command {
	TELNET_NEW_ENVIRON_IS = 0,
	TELNET_NEW_ENVIRON_SEND = 1,
	TELNET_NEW_ENVIRON_INFO = 2,
};
