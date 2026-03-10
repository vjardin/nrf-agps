/*
 * gps_assist_nrf.h - Inject pre-loaded A-GNSS data into nRF modem GNSS
 *
 * Copy this file and gps_assist_nrf.c into your Zephyr project alongside
 * gps_assist.h and the generated gps_assist_data.c.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <nrf_modem_gnss.h>
#include "gps_assist.h"

/*
 * Inject all assistance data (ephemerides, iono, UTC, system time,
 * location) into the nRF GNSS modem unconditionally.
 * Call after nrf_modem_gnss_init() and before starting a fix.
 *
 * Returns 0 on success, negative errno on failure.
 */
int gps_assist_inject(const struct gps_assist_data *data);

/*
 * Selective injection: only inject what the modem requests.
 * Pass the agnss_data_frame obtained from nrf_modem_gnss_read()
 * after an NRF_MODEM_GNSS_EVT_AGNSS_REQ event.
 *
 * Returns 0 on success, negative errno on failure.
 */
int gps_assist_inject_from_request(const struct gps_assist_data *data,
				   const struct nrf_modem_gnss_agnss_data_frame *req);

/*
 * Check what assistance data has expired and re-inject it.
 * Calls nrf_modem_gnss_agnss_expiry_get() to query the modem,
 * then re-injects any data whose expiry time is 0 (needed now).
 *
 * Returns 0 on success, negative errno on failure.
 */
int gps_assist_check_expiry(const struct gps_assist_data *data);

/*
 * Per-type injection functions for fine-grained control.
 */
int gps_assist_inject_ephemeris(const struct gps_assist_data *data,
				uint8_t prn);
int gps_assist_inject_utc(const struct gps_assist_data *data);
int gps_assist_inject_klobuchar(const struct gps_assist_data *data);
int gps_assist_inject_system_time(const struct gps_assist_data *data);
int gps_assist_inject_location(const struct gps_assist_data *data);
