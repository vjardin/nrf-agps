/*
 * gps_assist_nrf.c - Convert and inject A-GNSS data into nRF9151 modem
 *
 * Converts double-precision RINEX ephemeris to GPS ICD scaled integers
 * expected by nrf_modem_gnss_agnss_write().
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
#include <time.h>
#include <nrf_modem_gnss.h>

#include "gps_assist.h"
#include "gps_assist_nrf.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* GPS epoch: January 6, 1980 00:00:00 UTC */
#define GPS_EPOCH_UNIX 315964800L
#define GPS_LEAP_SECONDS 18

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
 *   tgd:      2^-31 s         (8-bit signed)
 *   toc/toe:  2^4  s          (16-bit unsigned)
 *   w:        2^-31 sc        (32-bit signed) [arg of perigee]
 *   delta_n:  2^-43 sc/s      (16-bit signed)
 *   M0:       2^-31 sc        (32-bit signed)
 *   OMEGA_DOT:2^-43 sc/s      (24-bit signed, stored in 32)
 *   e:        2^-33           (32-bit unsigned)
 *   IDOT:     2^-43 sc/s      (14-bit signed, stored in 16)
 *   sqrt_A:   2^-19 m^0.5     (32-bit unsigned)
 *   i0:       2^-31 sc        (32-bit signed)
 *   OMEGA0:   2^-31 sc        (32-bit signed)
 *   Crs/Crc:  2^-5  m         (16-bit signed)
 *   Cis/Cic:  2^-29 rad       (16-bit signed)
 *   Cus/Cuc:  2^-29 rad       (16-bit signed)
 */
static void convert_ephemeris(const struct gps_ephemeris *src,
			      struct nrf_modem_gnss_agnss_gps_data_ephemeris *dst)
{
	memset(dst, 0, sizeof(*dst));

	dst->sv_id  = src->prn;
	dst->health = src->health;
	dst->iodc   = src->iodc;

	/* Time parameters (scale factor 2^4 = 16) */
	dst->toc = (uint16_t)(src->toc / 16);
	dst->toe = (uint16_t)(src->toe / 16);

	/* Clock corrections */
	dst->af0 = (int32_t)round(src->af0 / 9.31322574615478515625e-10);  /* 2^-31 */
	dst->af1 = (int16_t)round(src->af1 / 1.13686837721616029739e-13);  /* 2^-43 */
	dst->af2 = (int8_t)round(src->af2 / 2.77555756156289135106e-17);   /* 2^-55 */

	/* Group delay */
	dst->tgd = (int8_t)round(src->tgd / 9.31322574615478515625e-10);  /* 2^-31 */

	/* Keplerian parameters (angles: radians -> semi-circles, then scale) */
	dst->w       = (int32_t)round(rad2sc(src->omega) / 4.65661287307739257812e-10);       /* 2^-31 */
	dst->delta_n = (int16_t)round(rad2sc(src->delta_n) / 1.13686837721616029739e-13);     /* 2^-43 */
	dst->m0      = (int32_t)round(rad2sc(src->m0) / 4.65661287307739257812e-10);           /* 2^-31 */
	dst->omega_dot = (int32_t)round(rad2sc(src->omega_dot) / 1.13686837721616029739e-13); /* 2^-43 */
	dst->e       = (uint32_t)round(src->e / 1.16415321826934814453e-10);                   /* 2^-33 */
	dst->idot    = (int16_t)round(rad2sc(src->idot) / 1.13686837721616029739e-13);         /* 2^-43 */
	dst->sqrt_a  = (uint32_t)round(src->sqrt_a / 1.90734863281250000000e-06);              /* 2^-19 */
	dst->i0      = (int32_t)round(rad2sc(src->i0) / 4.65661287307739257812e-10);           /* 2^-31 */
	dst->omega0  = (int32_t)round(rad2sc(src->omega0) / 4.65661287307739257812e-10);       /* 2^-31 */

	/* Harmonic corrections */
	dst->crs = (int16_t)round(src->crs / 3.12500000000000000000e-02);  /* 2^-5 */
	dst->cis = (int16_t)round(src->cis / 1.86264514923095703125e-09);  /* 2^-29 */
	dst->cus = (int16_t)round(src->cus / 1.86264514923095703125e-09);  /* 2^-29 */
	dst->crc = (int16_t)round(src->crc / 3.12500000000000000000e-02);  /* 2^-5 */
	dst->cic = (int16_t)round(src->cic / 1.86264514923095703125e-09);  /* 2^-29 */
	dst->cuc = (int16_t)round(src->cuc / 1.86264514923095703125e-09);  /* 2^-29 */
}

static void convert_iono(const struct gps_iono *src,
			 struct nrf_modem_gnss_agnss_data_klobuchar *dst)
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
	dst->beta0  = (int8_t)round(src->beta[0] / 2048.0);                       /* 2^11 */
	dst->beta1  = (int8_t)round(src->beta[1] / 16384.0);                      /* 2^14 */
	dst->beta2  = (int8_t)round(src->beta[2] / 65536.0);                      /* 2^16 */
	dst->beta3  = (int8_t)round(src->beta[3] / 65536.0);                      /* 2^16 */
}

static void convert_utc(const struct gps_utc *src,
			struct nrf_modem_gnss_agnss_gps_data_utc *dst)
{
	memset(dst, 0, sizeof(*dst));

	/*
	 * UTC parameter scale factors (IS-GPS-200 Table 20-IX):
	 *   A1:  2^-50 s/s   (25-bit signed, stored in 32)
	 *   A0:  2^-30 s     (32-bit signed)
	 *   tot: 2^12 s      (8-bit unsigned, range 0..147)
	 *   WNt: weeks mod 256 (8-bit unsigned)
	 */
	dst->a1        = (int32_t)round(src->a1 / 8.88178419700125232339e-16);  /* 2^-50 */
	dst->a0        = (int32_t)round(src->a0 / 9.31322574615478515625e-10);  /* 2^-30 */
	dst->tot       = (uint8_t)(src->tot / 4096);                             /* 2^12 */
	dst->wn_t      = (uint8_t)(src->wnt & 0xFF);
	dst->delta_tls = src->dt_ls;
}

/*
 * Convert generation timestamp to GPS system time.
 * GPS time = UTC + leap_seconds, referenced to GPS epoch (Jan 6, 1980).
 */
static void convert_system_time(const struct gps_assist_data *data,
				struct nrf_modem_gnss_agnss_gps_data_system_time_and_sv_tow *dst)
{
	memset(dst, 0, sizeof(*dst));

	time_t unix_t = (time_t)data->timestamp;
	long gps_t = (long)(unix_t - GPS_EPOCH_UNIX) + GPS_LEAP_SECONDS;

	long gps_days = gps_t / 86400;
	long time_of_day = gps_t % 86400;

	dst->date_day    = (uint16_t)gps_days;
	dst->time_full_s = (uint32_t)time_of_day;
	dst->time_frac_ms = 0;
	dst->sv_mask     = 0;  /* no per-SV TOW data */
}

/*
 * Convert approximate location to nRF modem format.
 * latitude:  coded N = (2^23 / 90) * degrees
 * longitude: coded N = (2^24 / 360) * degrees
 */
static void convert_location(const struct gps_location *src,
			     struct nrf_modem_gnss_agnss_data_location *dst)
{
	memset(dst, 0, sizeof(*dst));

	if (!src->valid) {
		dst->unc_semimajor = 255;
		dst->unc_semiminor = 255;
		dst->unc_altitude  = 255;
		return;
	}

	dst->latitude  = (int32_t)round(src->latitude * (double)(1 << 23) / 90.0);
	dst->longitude = (int32_t)round(src->longitude * (double)(1 << 24) / 360.0);
	dst->altitude  = src->altitude;

	/*
	 * Uncertainty: r = 10 * ((1.1)^K - 1) meters.
	 * K=18 gives ~55.6 km radius — good enough for a city-level hint.
	 * K=0 means 0 m uncertainty (exact).
	 */
	dst->unc_semimajor = 18;
	dst->unc_semiminor = 18;
	dst->unc_altitude  = 255;  /* altitude uncertainty unknown */
	dst->confidence    = 68;   /* ~1 sigma */
}

/*
 * Derive a reduced-precision almanac from ephemeris data.
 * RINEX broadcast files don't contain almanac records, but the modem
 * accepts them for coarse orbit prediction. All almanac fields are a
 * subset of the ephemeris with lower scale factors.
 *
 * GPS almanac scale factors (IS-GPS-200 Table 20-VI):
 *   e:         2^-21           (16-bit unsigned)
 *   toa:       2^12  s         (8-bit unsigned)
 *   delta_i:   2^-19 sc        (16-bit signed, offset from 0.3 sc = 54°)
 *   omega_dot: 2^-38 sc/s      (16-bit signed)
 *   sqrt_a:    2^-11 m^0.5     (24-bit unsigned)
 *   omega0:    2^-23 sc        (24-bit signed)
 *   w:         2^-23 sc        (24-bit signed)
 *   m0:        2^-23 sc        (24-bit signed)
 *   af0:       2^-20 s         (11-bit signed)
 *   af1:       2^-38 s/s       (11-bit signed)
 */
static void convert_almanac(const struct gps_ephemeris *src, uint16_t week,
			    struct nrf_modem_gnss_agnss_gps_data_almanac *dst)
{
	memset(dst, 0, sizeof(*dst));

	dst->sv_id     = src->prn;
	dst->wn        = (uint8_t)(week & 0xFF);
	dst->toa       = (uint8_t)(src->toe / 4096);
	dst->ioda      = 0;
	dst->sv_health = src->health;

	dst->e         = (uint16_t)round(src->e / 4.76837158203125e-07);                     /* 2^-21 */
	dst->sqrt_a    = (uint32_t)round(src->sqrt_a / 4.88281250000000000000e-04);           /* 2^-11 */

	/* delta_i = inclination - 0.3 semi-circles (nominal 54°) */
	double i0_sc = rad2sc(src->i0);
	dst->delta_i   = (int16_t)round((i0_sc - 0.3) / 1.90734863281250000000e-06);         /* 2^-19 */

	dst->omega_dot = (int16_t)round(rad2sc(src->omega_dot) / 3.63797880709171295166e-12); /* 2^-38 */
	dst->omega0    = (int32_t)round(rad2sc(src->omega0) / 1.19209289550781250000e-07);    /* 2^-23 */
	dst->w         = (int32_t)round(rad2sc(src->omega) / 1.19209289550781250000e-07);     /* 2^-23 */
	dst->m0        = (int32_t)round(rad2sc(src->m0) / 1.19209289550781250000e-07);        /* 2^-23 */

	dst->af0       = (int16_t)round(src->af0 / 9.53674316406250000000e-07);               /* 2^-20 */
	dst->af1       = (int16_t)round(src->af1 / 3.63797880709171295166e-12);               /* 2^-38 */
}

int gps_assist_inject_almanac(const struct gps_assist_data *data,
			      uint8_t prn)
{
	for (int i = 0; i < data->num_sv; i++) {
		if (data->sv[i].prn == prn) {
			struct nrf_modem_gnss_agnss_gps_data_almanac alm;

			convert_almanac(&data->sv[i], data->gps_week, &alm);
			return (int)nrf_modem_gnss_agnss_write(
				&alm, sizeof(alm),
				NRF_MODEM_GNSS_AGNSS_GPS_ALMANAC);
		}
	}
	return 0;  /* PRN not in dataset, not an error */
}

int gps_assist_inject_ephemeris(const struct gps_assist_data *data,
				uint8_t prn)
{
	for (int i = 0; i < data->num_sv; i++) {
		if (data->sv[i].prn == prn) {
			struct nrf_modem_gnss_agnss_gps_data_ephemeris eph;

			convert_ephemeris(&data->sv[i], &eph);
			return (int)nrf_modem_gnss_agnss_write(
				&eph, sizeof(eph),
				NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES);
		}
	}
	return 0;  /* PRN not in dataset, not an error */
}

int gps_assist_inject_utc(const struct gps_assist_data *data)
{
	struct nrf_modem_gnss_agnss_gps_data_utc utc;

	convert_utc(&data->utc, &utc);
	return (int)nrf_modem_gnss_agnss_write(
		&utc, sizeof(utc),
		NRF_MODEM_GNSS_AGNSS_GPS_UTC_PARAMETERS);
}

int gps_assist_inject_klobuchar(const struct gps_assist_data *data)
{
	struct nrf_modem_gnss_agnss_data_klobuchar iono;

	convert_iono(&data->iono, &iono);
	return (int)nrf_modem_gnss_agnss_write(
		&iono, sizeof(iono),
		NRF_MODEM_GNSS_AGNSS_KLOBUCHAR_IONOSPHERIC_CORRECTION);
}

int gps_assist_inject_system_time(const struct gps_assist_data *data)
{
	struct nrf_modem_gnss_agnss_gps_data_system_time_and_sv_tow st;

	convert_system_time(data, &st);
	return (int)nrf_modem_gnss_agnss_write(
		&st, sizeof(st),
		NRF_MODEM_GNSS_AGNSS_GPS_SYSTEM_CLOCK_AND_TOWS);
}

int gps_assist_inject_location(const struct gps_assist_data *data)
{
	struct nrf_modem_gnss_agnss_data_location loc;

	convert_location(&data->location, &loc);
	return (int)nrf_modem_gnss_agnss_write(
		&loc, sizeof(loc),
		NRF_MODEM_GNSS_AGNSS_LOCATION);
}

int gps_assist_inject(const struct gps_assist_data *data)
{
	int err;

	/* Inject ephemerides and almanacs */
	for (int i = 0; i < data->num_sv; i++) {
		struct nrf_modem_gnss_agnss_gps_data_ephemeris eph;

		convert_ephemeris(&data->sv[i], &eph);
		err = (int)nrf_modem_gnss_agnss_write(
			&eph, sizeof(eph),
			NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES);
		if (err)
			return err;

		struct nrf_modem_gnss_agnss_gps_data_almanac alm;

		convert_almanac(&data->sv[i], data->gps_week, &alm);
		err = (int)nrf_modem_gnss_agnss_write(
			&alm, sizeof(alm),
			NRF_MODEM_GNSS_AGNSS_GPS_ALMANAC);
		if (err)
			return err;
	}

	/* Inject ionospheric correction */
	struct nrf_modem_gnss_agnss_data_klobuchar iono;

	convert_iono(&data->iono, &iono);
	err = (int)nrf_modem_gnss_agnss_write(
		&iono, sizeof(iono),
		NRF_MODEM_GNSS_AGNSS_KLOBUCHAR_IONOSPHERIC_CORRECTION);
	if (err)
		return err;

	/* Inject UTC parameters */
	struct nrf_modem_gnss_agnss_gps_data_utc utc;

	convert_utc(&data->utc, &utc);
	err = (int)nrf_modem_gnss_agnss_write(
		&utc, sizeof(utc),
		NRF_MODEM_GNSS_AGNSS_GPS_UTC_PARAMETERS);
	if (err)
		return err;

	/* Inject system time */
	struct nrf_modem_gnss_agnss_gps_data_system_time_and_sv_tow st;

	convert_system_time(data, &st);
	err = (int)nrf_modem_gnss_agnss_write(
		&st, sizeof(st),
		NRF_MODEM_GNSS_AGNSS_GPS_SYSTEM_CLOCK_AND_TOWS);
	if (err)
		return err;

	/* Inject location if valid */
	if (data->location.valid) {
		struct nrf_modem_gnss_agnss_data_location loc;

		convert_location(&data->location, &loc);
		err = (int)nrf_modem_gnss_agnss_write(
			&loc, sizeof(loc),
			NRF_MODEM_GNSS_AGNSS_LOCATION);
		if (err)
			return err;
	}

	return 0;
}

int gps_assist_check_expiry(const struct gps_assist_data *data)
{
	struct nrf_modem_gnss_agnss_expiry exp;
	int32_t ret;

	ret = nrf_modem_gnss_agnss_expiry_get(&exp);
	if (ret)
		return (int)ret;

	/* Build a selective request from expiry info */
	struct nrf_modem_gnss_agnss_data_frame req;

	memset(&req, 0, sizeof(req));
	req.data_flags = exp.data_flags;

	/* Collect expired ephemeris and almanac SVs into bitmasks */
	uint64_t ephe_mask = 0;
	uint64_t alm_mask  = 0;

	for (int i = 0; i < exp.sv_count; i++) {
		if (exp.sv[i].system_id != NRF_MODEM_GNSS_SYSTEM_GPS)
			continue;
		if (exp.sv[i].sv_id < 1 || exp.sv[i].sv_id > 32)
			continue;
		uint64_t bit = (uint64_t)1 << (exp.sv[i].sv_id - 1);

		if (exp.sv[i].ephe_expiry == 0)
			ephe_mask |= bit;
		if (exp.sv[i].alm_expiry == 0)
			alm_mask |= bit;
	}

	if (ephe_mask || alm_mask) {
		req.system_count = 1;
		req.system[0].system_id = NRF_MODEM_GNSS_SYSTEM_GPS;
		req.system[0].sv_mask_ephe = ephe_mask;
		req.system[0].sv_mask_alm  = alm_mask;
	}

	/* Nothing to do */
	if (!req.data_flags && !ephe_mask && !alm_mask)
		return 0;

	return gps_assist_inject_from_request(data, &req);
}

int gps_assist_inject_from_request(const struct gps_assist_data *data,
				   const struct nrf_modem_gnss_agnss_data_frame *req)
{
	int err;

	/* Per-SV ephemeris and almanac injection based on request bitmasks */
	for (int s = 0; s < req->system_count; s++) {
		if (req->system[s].system_id != NRF_MODEM_GNSS_SYSTEM_GPS)
			continue;

		uint64_t ephe_mask = req->system[s].sv_mask_ephe;
		uint64_t alm_mask  = req->system[s].sv_mask_alm;

		for (int prn = 1; prn <= 32; prn++) {
			uint64_t bit = (uint64_t)1 << (prn - 1);

			if (ephe_mask & bit) {
				err = gps_assist_inject_ephemeris(data,
								 (uint8_t)prn);
				if (err)
					return err;
			}
			if (alm_mask & bit) {
				err = gps_assist_inject_almanac(data,
							       (uint8_t)prn);
				if (err)
					return err;
			}
		}
	}

	if (req->data_flags & NRF_MODEM_GNSS_AGNSS_GPS_UTC_REQUEST) {
		err = gps_assist_inject_utc(data);
		if (err)
			return err;
	}

	if (req->data_flags & NRF_MODEM_GNSS_AGNSS_KLOBUCHAR_REQUEST) {
		err = gps_assist_inject_klobuchar(data);
		if (err)
			return err;
	}

	if (req->data_flags & NRF_MODEM_GNSS_AGNSS_GPS_SYS_TIME_AND_SV_TOW_REQUEST) {
		err = gps_assist_inject_system_time(data);
		if (err)
			return err;
	}

	if (req->data_flags & NRF_MODEM_GNSS_AGNSS_POSITION_REQUEST) {
		err = gps_assist_inject_location(data);
		if (err)
			return err;
	}

	return 0;
}
