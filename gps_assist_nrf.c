/*
 * gps_assist_nrf.c - Convert and inject A-GPS data into nRF9151 modem
 *
 * Converts double-precision RINEX ephemeris to GPS ICD scaled integers
 * expected by nrf_modem_gnss_agps_write().
 *
 * Scale factors from IS-GPS-200 (GPS Interface Control Document).
 * Angular values: RINEX stores radians, GPS ICD uses semi-circles
 * (1 semi-circle = PI radians).
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <math.h>
#include <string.h>
#include <nrf_modem_gnss.h>

#include "gps_assist.h"
#include "gps_assist_nrf.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Convert radians to semi-circles */
static inline double rad2sc(double rad)
{
	return rad / M_PI;
}

/*
 * Convert one ephemeris record from doubles to nRF modem format.
 *
 * GPS ICD scale factors (IS-GPS-200 Table 20-I/III):
 *   af0:      2^-31 s         (22-bit signed)
 *   af1:      2^-43 s/s       (16-bit signed)
 *   af2:      2^-55 s/s^2     (8-bit signed)
 *   Crs:      2^-5  m         (16-bit signed)
 *   delta_n:  2^-43 sc/s      (16-bit signed)
 *   M0:       2^-31 sc        (32-bit signed)
 *   Cuc:      2^-29 rad       (16-bit signed)
 *   e:        2^-33           (32-bit unsigned)
 *   Cus:      2^-29 rad       (16-bit signed)
 *   sqrt_A:   2^-19 m^0.5     (32-bit unsigned)
 *   toe:      16 s            (16-bit unsigned)
 *   Cic:      2^-29 rad       (16-bit signed)
 *   OMEGA0:   2^-31 sc        (32-bit signed)
 *   Cis:      2^-29 rad       (16-bit signed)
 *   i0:       2^-31 sc        (32-bit signed)
 *   Crc:      2^-5  m         (16-bit signed)
 *   omega:    2^-31 sc        (32-bit signed)
 *   OMEGA_DOT:2^-43 sc/s      (24-bit signed, stored in 32)
 *   IDOT:     2^-43 sc/s      (14-bit signed, stored in 16)
 *   TGD:      2^-31 s         (8-bit signed)
 *   toc:      16 s            (16-bit unsigned)
 */
static void convert_ephemeris(const struct gps_ephemeris *src,
			      struct nrf_modem_gnss_agps_data_ephemeris *dst)
{
	memset(dst, 0, sizeof(*dst));

	dst->sv_id  = src->prn;
	dst->health = src->health;
	dst->iodc   = src->iodc;
	dst->iode   = src->iode;

	/* Time parameters (seconds / 16) */
	dst->toc = (uint16_t)(src->toc / 16);
	dst->toe = (uint16_t)(src->toe / 16);

	/* Clock corrections */
	dst->af0 = (int32_t)round(src->af0 / 9.31322574615478515625e-10);  /* 2^-31 */
	dst->af1 = (int16_t)round(src->af1 / 1.13686837721616029739e-13);  /* 2^-43 */
	dst->af2 = (int8_t)round(src->af2 / 2.77555756156289135106e-17);   /* 2^-55 */

	/* Keplerian parameters (angles: radians → semi-circles, then scale) */
	dst->m0      = (int32_t)round(rad2sc(src->m0) / 4.65661287307739257812e-10);       /* 2^-31 */
	dst->delta_n = (int16_t)round(rad2sc(src->delta_n) / 1.13686837721616029739e-13);  /* 2^-43 */
	dst->e       = (uint32_t)round(src->e / 1.16415321826934814453e-10);                /* 2^-33 */
	dst->sqrt_a  = (uint32_t)round(src->sqrt_a / 1.90734863281250000000e-06);           /* 2^-19 */
	dst->omega0  = (int32_t)round(rad2sc(src->omega0) / 4.65661287307739257812e-10);    /* 2^-31 */
	dst->i0      = (int32_t)round(rad2sc(src->i0) / 4.65661287307739257812e-10);        /* 2^-31 */
	dst->omega   = (int32_t)round(rad2sc(src->omega) / 4.65661287307739257812e-10);     /* 2^-31 */

	/* Rate terms */
	dst->omega_dot = (int32_t)round(rad2sc(src->omega_dot) / 1.13686837721616029739e-13); /* 2^-43 */
	dst->idot      = (int16_t)round(rad2sc(src->idot) / 1.13686837721616029739e-13);      /* 2^-43 */

	/* Harmonic corrections */
	dst->cuc = (int16_t)round(src->cuc / 1.86264514923095703125e-09);  /* 2^-29 */
	dst->cus = (int16_t)round(src->cus / 1.86264514923095703125e-09);  /* 2^-29 */
	dst->crc = (int16_t)round(src->crc / 3.12500000000000000000e-02);  /* 2^-5 */
	dst->crs = (int16_t)round(src->crs / 3.12500000000000000000e-02);  /* 2^-5 */
	dst->cic = (int16_t)round(src->cic / 1.86264514923095703125e-09);  /* 2^-29 */
	dst->cis = (int16_t)round(src->cis / 1.86264514923095703125e-09);  /* 2^-29 */

	/* Group delay */
	dst->tgd = (int8_t)round(src->tgd / 9.31322574615478515625e-10);  /* 2^-31 */
}

static void convert_iono(const struct gps_iono *src,
			 struct nrf_modem_gnss_agps_data_klobuchar *dst)
{
	memset(dst, 0, sizeof(*dst));

	/*
	 * Klobuchar iono scale factors (IS-GPS-200 Table 20-X):
	 *   alpha0: 2^-30    alpha1: 2^-27    alpha2: 2^-24    alpha3: 2^-24
	 *   beta0:  2^11     beta1:  2^14     beta2:  2^16     beta3:  2^16
	 */
	dst->alpha0 = (int8_t)round(src->alpha[0] / 9.31322574615478515625e-10);  /* 2^-30 */
	dst->alpha1 = (int8_t)round(src->alpha[1] / 7.45058059692382812500e-09);  /* 2^-27 */
	dst->alpha2 = (int8_t)round(src->alpha[2] / 5.96046447753906250000e-08);  /* 2^-24 */
	dst->alpha3 = (int8_t)round(src->alpha[3] / 5.96046447753906250000e-08);  /* 2^-24 */
	dst->beta0  = (int8_t)round(src->beta[0] / 2048.0);                        /* 2^11 */
	dst->beta1  = (int8_t)round(src->beta[1] / 16384.0);                       /* 2^14 */
	dst->beta2  = (int8_t)round(src->beta[2] / 65536.0);                       /* 2^16 */
	dst->beta3  = (int8_t)round(src->beta[3] / 65536.0);                       /* 2^16 */
}

static void convert_utc(const struct gps_utc *src,
			struct nrf_modem_gnss_agps_data_utc *dst)
{
	memset(dst, 0, sizeof(*dst));

	/*
	 * UTC parameter scale factors (IS-GPS-200 Table 20-IX):
	 *   A0:  2^-30 s     (32-bit signed)
	 *   A1:  2^-50 s/s   (24-bit signed, stored in 32)
	 *   tot: 2^12 s      (8-bit unsigned)
	 *   WNt: weeks       (8-bit unsigned)
	 */
	dst->a0    = (int32_t)round(src->a0 / 9.31322574615478515625e-10);  /* 2^-30 */
	dst->a1    = (int32_t)round(src->a1 / 8.88178419700125232339e-16);  /* 2^-50 */
	dst->tot   = (uint8_t)(src->tot / 4096);                             /* 2^12 */
	dst->wn_t  = (uint8_t)(src->wnt & 0xFF);
	dst->dt_ls = src->dt_ls;
}

int gps_assist_inject(const struct gps_assist_data *data)
{
	int err;

	/* Inject ephemerides */
	for (int i = 0; i < data->num_sv; i++) {
		struct nrf_modem_gnss_agps_data_ephemeris eph;

		convert_ephemeris(&data->sv[i], &eph);

		err = nrf_modem_gnss_agps_write(
			&eph, sizeof(eph),
			NRF_MODEM_GNSS_AGPS_GPS_EPHEMERIDES);
		if (err)
			return err;
	}

	/* Inject ionospheric correction */
	struct nrf_modem_gnss_agps_data_klobuchar iono;

	convert_iono(&data->iono, &iono);
	err = nrf_modem_gnss_agps_write(
		&iono, sizeof(iono),
		NRF_MODEM_GNSS_AGPS_KLOBUCHAR_IONOSPHERIC_CORRECTION);
	if (err)
		return err;

	/* Inject UTC parameters */
	struct nrf_modem_gnss_agps_data_utc utc;

	convert_utc(&data->utc, &utc);
	err = nrf_modem_gnss_agps_write(
		&utc, sizeof(utc),
		NRF_MODEM_GNSS_AGPS_GPS_UTC_PARAMETERS);
	if (err)
		return err;

	return 0;
}
