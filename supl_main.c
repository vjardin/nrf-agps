/*
 * supl_main.c - SUPL 2.0 A-GNSS server entry point
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "supl_server.h"

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Options:\n"
		"  -d DB_PATH    SQLite database path (required)\n"
		"  -p PORT       Listen port (default: 7275 TLS, 7276 plain)\n"
		"  -c CERT_FILE  TLS certificate (PEM)\n"
		"  -k KEY_FILE   TLS private key (PEM)\n"
		"  --no-tls      Plain TCP mode (port defaults to 7276)\n"
		"  -l ADDR       Bind address (default: 0.0.0.0)\n"
		"  -v            Verbose logging\n"
		"  -h            Show this help\n",
		prog);
}

int main(int argc, char *argv[])
{
	struct supl_server_config cfg = {0};

	static struct option long_opts[] = {
		{"no-tls", no_argument, NULL, 'T'},
		{"help",   no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	int opt;

	while ((opt = getopt_long(argc, argv, "d:p:c:k:l:vh",
				  long_opts, NULL)) != -1) {
		switch (opt) {
		case 'd':
			cfg.db_path = optarg;
			break;
		case 'p':
			cfg.port = atoi(optarg);
			break;
		case 'c':
			cfg.cert_file = optarg;
			break;
		case 'k':
			cfg.key_file = optarg;
			break;
		case 'l':
			cfg.bind_addr = optarg;
			break;
		case 'v':
			cfg.verbose = 1;
			break;
		case 'T':
			cfg.no_tls = 1;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	if (!cfg.db_path) {
		fprintf(stderr, "Error: -d DB_PATH is required\n\n");
		usage(argv[0]);
		return 1;
	}

	if (!cfg.no_tls && (!cfg.cert_file || !cfg.key_file)) {
		fprintf(stderr,
			"Error: TLS mode requires both -c CERT and -k KEY\n"
			"       Use --no-tls for plain TCP mode\n\n");
		usage(argv[0]);
		return 1;
	}

	return supl_server_run(&cfg) == 0 ? 0 : 1;
}
