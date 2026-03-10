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
 * Per-type injection function for fine-grained control.
 */
int gps_assist_inject_location(const struct gps_assist_data *data);
