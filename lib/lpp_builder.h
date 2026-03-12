/*
 * lpp_builder.h - Build LPP ProvideAssistanceData from GPS assist data
 *
 * Populates 3GPP TS 37.355 ASN.1 structures with GPS/QZSS assistance data
 * and encodes them to UPER bitstream.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gps_assist.h"

/*
 * Build a complete LPP ProvideAssistanceData PDU from GPS assist data.
 * Encodes all available data: ephemeris, almanac, ionosphere, UTC,
 * reference time, and reference location for GPS and QZSS.
 *
 * out_buf: receives a malloc'd UPER-encoded buffer (caller must free)
 * out_len: receives the buffer length in bytes
 *
 * Returns 0 on success, -1 on error.
 */
int lpp_build_assistance_data(const struct gps_assist_data *data,
			      uint8_t **out_buf, size_t *out_len);
