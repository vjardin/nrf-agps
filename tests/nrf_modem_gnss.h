/*
 * nrf_modem_gnss.h - Mock nRF modem GNSS API for host-side testing
 *
 * Struct definitions and constants matching nrf_modem_gnss.h from
 * nrfconnect/sdk-nrfxlib so gps_assist_nrf.c compiles on the build host.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stdint.h>

/* A-GNSS data type enumerator (nrf_modem_gnss_agnss_write type param) */
#define NRF_MODEM_GNSS_AGNSS_GPS_UTC_PARAMETERS               1
#define NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES                   2
#define NRF_MODEM_GNSS_AGNSS_KLOBUCHAR_IONOSPHERIC_CORRECTION  4

/*
 * GPS ICD scaled-integer structs (IS-GPS-200).
 * Field names, types, and ordering match nrf_modem_gnss.h from sdk-nrfxlib.
 */

struct nrf_modem_gnss_agnss_gps_data_utc {
	int32_t a1;          /* 2^-50 s/s (25-bit signed) */
	int32_t a0;          /* 2^-30 s */
	uint8_t tot;         /* seconds / 4096 (range 0..147) */
	uint8_t wn_t;        /* GPS week mod 256 */
	int8_t  delta_tls;   /* current leap seconds */
	uint8_t wn_lsf;      /* leap second reference week mod 256 */
	int8_t  dn;          /* leap second reference day 1..7 */
	int8_t  delta_tlsf;  /* future leap seconds */
};

struct nrf_modem_gnss_agnss_gps_data_ephemeris {
	uint8_t  sv_id;      /* PRN 1-32 (GPS), 193-202 (QZSS) */
	uint8_t  health;
	uint16_t iodc;       /* range 0..2047 */
	uint16_t toc;        /* 2^4 s (range 0..37799) */
	int8_t   af2;        /* 2^-55 s/s^2 */
	int16_t  af1;        /* 2^-43 s/s */
	int32_t  af0;        /* 2^-31 s (22-bit signed) */
	int8_t   tgd;        /* 2^-31 s */
	uint8_t  ura;        /* URA index 0..15 */
	uint8_t  fit_int;    /* curve fit interval 0..1 */
	uint16_t toe;        /* 2^4 s (range 0..37799) */
	int32_t  w;          /* argument of perigee, 2^-31 sc */
	int16_t  delta_n;    /* 2^-43 sc/s */
	int32_t  m0;         /* 2^-31 sc */
	int32_t  omega_dot;  /* 2^-43 sc/s (24-bit signed) */
	uint32_t e;          /* 2^-33 */
	int16_t  idot;       /* 2^-43 sc/s (14-bit signed) */
	uint32_t sqrt_a;     /* 2^-19 m^0.5 */
	int32_t  i0;         /* 2^-31 sc */
	int32_t  omega0;     /* 2^-31 sc */
	int16_t  crs;        /* 2^-5 m */
	int16_t  cis;        /* 2^-29 rad */
	int16_t  cus;        /* 2^-29 rad */
	int16_t  crc;        /* 2^-5 m */
	int16_t  cic;        /* 2^-29 rad */
	int16_t  cuc;        /* 2^-29 rad */
};

struct nrf_modem_gnss_agnss_data_klobuchar {
	int8_t alpha0;       /* 2^-30 */
	int8_t alpha1;       /* 2^-27 */
	int8_t alpha2;       /* 2^-24 */
	int8_t alpha3;       /* 2^-24 */
	int8_t beta0;        /* 2^11 */
	int8_t beta1;        /* 2^14 */
	int8_t beta2;        /* 2^16 */
	int8_t beta3;        /* 2^16 */
};

#define NRF_MODEM_GNSS_AGNSS_GPS_SYSTEM_CLOCK_AND_TOWS         6
#define NRF_MODEM_GNSS_AGNSS_LOCATION                          7

#define NRF_MODEM_GNSS_AGNSS_GPS_MAX_SV_TOW 32

struct nrf_modem_gnss_agnss_gps_data_tow_element {
	uint16_t tlm;        /* TLM word (2 reserved + 14 TLM bits) */
	uint8_t  flags;      /* bit 0: anti-spoof, bit 1: alert */
};

struct nrf_modem_gnss_agnss_gps_data_system_time_and_sv_tow {
	uint16_t date_day;   /* days since Jan 6, 1980 00:00:00 UTC */
	uint32_t time_full_s;/* seconds of day 0..86399 */
	uint16_t time_frac_ms;/* fractional ms 0..999 */
	uint32_t sv_mask;    /* PRN bitmask for valid TOW entries */
	struct nrf_modem_gnss_agnss_gps_data_tow_element
		sv_tow[NRF_MODEM_GNSS_AGNSS_GPS_MAX_SV_TOW];
};

struct nrf_modem_gnss_agnss_data_location {
	int32_t  latitude;       /* coded: N <= (2^23/90) * deg */
	int32_t  longitude;      /* coded: N <= (2^24/360) * deg */
	int16_t  altitude;       /* meters above WGS-84 */
	uint8_t  unc_semimajor;  /* 0..127, 255=invalid lat/lon */
	uint8_t  unc_semiminor;  /* 0..127, 255=invalid lat/lon */
	uint8_t  orientation_major; /* degrees 0..179 */
	uint8_t  unc_altitude;   /* 0..127, 255=invalid altitude */
	uint8_t  confidence;     /* percent 0..128, 0=no info */
};

/*
 * Mock write function — records calls for verification.
 * Defined in test_nrf_convert.c.
 */
int32_t nrf_modem_gnss_agnss_write(void *buf, int32_t buf_len, uint16_t type);
