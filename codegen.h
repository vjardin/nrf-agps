/*
 * codegen.h - C source file generator for GPS assistance data
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include "gps_assist.h"

/*
 * Generate gps_assist_data.c from parsed RINEX data.
 * prefix: output file path prefix (e.g. "gps_assist_data")
 * source_name: RINEX source filename for comment header
 * Returns 0 on success, -1 on error.
 */
int codegen_write(const char *prefix, const struct gps_assist_data *data,
		  const char *source_name);
