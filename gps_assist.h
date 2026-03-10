/*
 * gps_assist.h - GPS assistance data structures
 *
 * Portable header shared between the server-side RINEX tool and Zephyr
 * firmware. Stores ephemeris as doubles (matching RINEX representation).
 * Conversion to nRF modem GPS ICD format is done by gps_assist_nrf.c.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#ifdef __ZEPHYR__
#include <zephyr/types.h>
#else
#include <stdint.h>
#endif

#define GPS_MAX_SATS 32

struct gps_ephemeris {
	uint8_t  prn;       /* PRN number (1-32) */
	uint8_t  health;    /* SV health (0 = healthy) */
	uint16_t iodc;      /* Issue of Data, Clock */
	uint8_t  iode;      /* Issue of Data, Ephemeris */
	uint16_t week;      /* GPS week number */
	uint32_t toe;       /* Time of ephemeris (s into GPS week) */
	uint32_t toc;       /* Time of clock (s into GPS week) */
	/* Clock corrections */
	double af0;         /* SV clock bias (s) */
	double af1;         /* SV clock drift (s/s) */
	double af2;         /* SV clock drift rate (s/s^2) */
	/* Keplerian orbital parameters */
	double sqrt_a;      /* Square root of semi-major axis (m^0.5) */
	double e;           /* Eccentricity */
	double i0;          /* Inclination at reference time (rad) */
	double omega0;      /* Longitude of ascending node at weekly epoch (rad) */
	double omega;       /* Argument of perigee (rad) */
	double m0;          /* Mean anomaly at reference time (rad) */
	/* Correction terms */
	double delta_n;     /* Mean motion difference (rad/s) */
	double omega_dot;   /* Rate of right ascension (rad/s) */
	double idot;        /* Rate of inclination angle (rad/s) */
	/* Harmonic correction coefficients */
	double cuc;         /* Cosine correction to argument of latitude (rad) */
	double cus;         /* Sine correction to argument of latitude (rad) */
	double crc;         /* Cosine correction to orbit radius (m) */
	double crs;         /* Sine correction to orbit radius (m) */
	double cic;         /* Cosine correction to inclination (rad) */
	double cis;         /* Sine correction to inclination (rad) */
	/* Group delay */
	double tgd;         /* Total group delay (s) */
};

struct gps_iono {
	double alpha[4];    /* Klobuchar ionospheric alpha parameters */
	double beta[4];     /* Klobuchar ionospheric beta parameters */
};

struct gps_utc {
	double   a0;        /* UTC correction coefficient (s) */
	double   a1;        /* UTC correction coefficient (s/s) */
	uint32_t tot;       /* Reference time for UTC data (s) */
	uint16_t wnt;       /* UTC reference week number */
	int8_t   dt_ls;     /* Current UTC leap seconds (s) */
};

struct gps_location {
	double  latitude;   /* Geodetic latitude in degrees (-90..90) */
	double  longitude;  /* Geodetic longitude in degrees (-180..180) */
	int16_t altitude;   /* Meters above WGS-84 ellipsoid */
	uint8_t valid;      /* Non-zero if lat/lon are populated */
};

struct gps_assist_data {
	uint32_t           timestamp;            /* Unix time of generation */
	uint16_t           gps_week;             /* Reference GPS week */
	uint8_t            num_sv;               /* Number of satellite entries */
	struct gps_iono    iono;                 /* Ionospheric model */
	struct gps_utc     utc;                  /* UTC parameters */
	struct gps_location location;            /* Approximate reference location */
	struct gps_ephemeris sv[GPS_MAX_SATS];   /* Satellite ephemerides */
};

/* Declared in the generated gps_assist_data.c */
extern const struct gps_assist_data gps_assist;
