/*
 * nrf_modem_gnss_mock.h - Mock nRF modem GNSS API for host-side testing
 *
 * Provides struct definitions and API stubs matching the nrf_modem_gnss.h
 * interface used by gps_assist_nrf.c, so the conversion code can be
 * compiled and tested on the build host without the nRF Connect SDK.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stdint.h>

/*
 * GPS ICD scaled-integer structs (IS-GPS-200).
 * Field names and types match nrf_modem_gnss.h from nrfxlib.
 */

struct nrf_modem_gnss_agps_data_ephemeris {
	uint8_t  sv_id;
	uint8_t  health;
	uint16_t iodc;
	uint16_t toc;       /* seconds / 16 */
	int8_t   af2;       /* 2^-55 s/s^2 */
	int16_t  af1;       /* 2^-43 s/s */
	int32_t  af0;       /* 2^-31 s */
	uint8_t  iode;
	int16_t  crs;       /* 2^-5  m */
	int16_t  delta_n;   /* 2^-43 sc/s */
	int32_t  m0;        /* 2^-31 sc */
	int16_t  cuc;       /* 2^-29 rad */
	uint32_t e;         /* 2^-33 */
	int16_t  cus;       /* 2^-29 rad */
	uint32_t sqrt_a;    /* 2^-19 m^0.5 */
	uint16_t toe;       /* seconds / 16 */
	int16_t  cic;       /* 2^-29 rad */
	int32_t  omega0;    /* 2^-31 sc */
	int16_t  cis;       /* 2^-29 rad */
	int32_t  i0;        /* 2^-31 sc */
	int16_t  crc;       /* 2^-5  m */
	int32_t  omega;     /* 2^-31 sc */
	int32_t  omega_dot; /* 2^-43 sc/s */
	int16_t  idot;      /* 2^-43 sc/s */
	int8_t   tgd;       /* 2^-31 s */
};

struct nrf_modem_gnss_agps_data_klobuchar {
	int8_t alpha0;      /* 2^-30 */
	int8_t alpha1;      /* 2^-27 */
	int8_t alpha2;      /* 2^-24 */
	int8_t alpha3;      /* 2^-24 */
	int8_t beta0;       /* 2^11 */
	int8_t beta1;       /* 2^14 */
	int8_t beta2;       /* 2^16 */
	int8_t beta3;       /* 2^16 */
};

struct nrf_modem_gnss_agps_data_utc {
	int32_t a0;         /* 2^-30 s */
	int32_t a1;         /* 2^-50 s/s */
	uint8_t tot;        /* seconds / 4096 */
	uint8_t wn_t;
	int8_t  dt_ls;
	uint8_t wn_lsf;
	int8_t  dn;
	int8_t  dt_lsf;
};

enum {
	NRF_MODEM_GNSS_AGPS_GPS_UTC_PARAMETERS              = 1,
	NRF_MODEM_GNSS_AGPS_GPS_EPHEMERIDES                 = 2,
	NRF_MODEM_GNSS_AGPS_KLOBUCHAR_IONOSPHERIC_CORRECTION = 3,
};

/*
 * Mock write function — records calls for verification.
 * Defined in test_nrf_convert.c.
 */
int nrf_modem_gnss_agps_write(const void *buf, int32_t buf_len,
			      uint16_t type);
