/*
 * sqlitedb.h - SQLite storage for GPS assistance data
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include "gps_assist.h"

/*
 * Store GPS assistance data in a SQLite database.
 * Creates the database and tables if they don't exist.
 * Each call inserts a new dataset (metadata + ephemeris + almanac + iono + utc).
 * db_path: path to SQLite database file
 * source_name: RINEX source filename for metadata
 * Returns 0 on success, -1 on error.
 */
int sqlitedb_store(const char *db_path, const struct gps_assist_data *data,
		   const char *source_name);
