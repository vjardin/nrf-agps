/*
 * rinex.h - RINEX navigation file download and parser
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include "gps_assist.h"

/*
 * Download RINEX BRDC navigation file for given date.
 * Returns malloc'd path to downloaded file, or NULL on error.
 */
char *rinex_download(int year, int doy, const char *url_override);

/*
 * Parse a RINEX v3/v4 navigation file (.rnx or .rnx.gz).
 * Extracts GPS ephemerides, ionospheric and UTC parameters.
 * Returns 0 on success, -1 on error.
 */
int rinex_parse(const char *path, struct gps_assist_data *out);
