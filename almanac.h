/*
 * almanac.h - GPS almanac download and parser (SEM/YUMA formats)
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include "gps_assist.h"

/* USCG NAVCEN current SEM almanac (no authentication required) */
#define ALMANAC_SEM_URL \
	"https://www.navcen.uscg.gov/sites/default/files/gps/almanac/current_sem.al3"

/*
 * Download the current SEM almanac from USCG NAVCEN and parse it.
 * Populates data->alm[] and data->num_alm.
 * Returns 0 on success, -1 on error.
 */
int almanac_download_sem(struct gps_assist_data *data);

/*
 * Parse a local SEM almanac file (.al3).
 * Returns 0 on success, -1 on error.
 */
int almanac_parse_sem(const char *path, struct gps_assist_data *data);

/*
 * Parse a local YUMA almanac file.
 * Converts radians to semi-circles on read.
 * Returns 0 on success, -1 on error.
 */
int almanac_parse_yuma(const char *path, struct gps_assist_data *data);
