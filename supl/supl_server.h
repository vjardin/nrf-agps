/*
 * supl_server.h - SUPL 2.0 A-GNSS server over TCP/TLS
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

struct supl_server_config {
	const char *db_path;    /* SQLite database path (required) */
	const char *bind_addr;  /* Bind address (default: "0.0.0.0") */
	int         port;       /* Listen port (default: 7275 TLS, 7276 plain) */
	const char *cert_file;  /* TLS certificate PEM (NULL for plain TCP) */
	const char *key_file;   /* TLS private key PEM (NULL for plain TCP) */
	int         no_tls;     /* 1 = plain TCP mode */
	int         verbose;    /* 1 = verbose logging */
};

/*
 * Run the SUPL server (blocks until signal).
 * Returns 0 on clean shutdown, -1 on error.
 */
int supl_server_run(const struct supl_server_config *cfg);
