/*
 * test_nrf_convert.c - GPS ICD conversion tests for nRF modem helpers
 *
 * Tests the double -> scaled-integer conversion against known values
 * and verifies round-trip accuracy within LSB tolerance.
 *
 * Compiles gps_assist_nrf.c against a mock nrf_modem_gnss.h so the
 * conversion code can run on the build host.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <time.h>

/*
 * The mock header is picked up by gps_assist_nrf.c via -I tests/
 * which shadows the real <nrf_modem_gnss.h>.
 */
#include <nrf_modem_gnss.h>
#include "gps_assist.h"
#include "gps_assist_nrf.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int tests_run;
static int tests_passed;

#define TEST(name) do { \
	tests_run++; \
	fprintf(stderr, "  %-50s", name); \
} while (0)

#define PASS() do { \
	tests_passed++; \
	fprintf(stderr, " OK\n"); \
} while (0)

#define FAIL(msg, ...) do { \
	fprintf(stderr, " FAIL\n    " msg "\n", ##__VA_ARGS__); \
} while (0)

#define ASSERT_INT_EQ(a, b) do { \
	if ((a) != (b)) { FAIL("%s = %d, expected %d", #a, (int)(a), (int)(b)); goto out; } \
} while (0)

#define ASSERT_DBL_NEAR(a, b, tol) do { \
	if (fabs((a) - (b)) > (tol)) { \
		FAIL("%s = %.15e, expected %.15e (tol %.e)", #a, (double)(a), (double)(b), (tol)); \
		goto out; \
	} \
} while (0)

#define ASSERT_TRUE(cond) do { \
	if (!(cond)) { FAIL("assertion failed: %s", #cond); goto out; } \
} while (0)

static int mock_write_count;
static uint16_t mock_write_types[64];
static int32_t  mock_write_sizes[64];

int32_t nrf_modem_gnss_agnss_write(void *buf, int32_t buf_len,
				   uint16_t type)
{
	(void)buf;
	if (mock_write_count < 64) {
		mock_write_types[mock_write_count] = type;
		mock_write_sizes[mock_write_count] = buf_len;
		mock_write_count++;
	}
	return 0;
}

static struct nrf_modem_gnss_agnss_expiry mock_expiry;
static int32_t mock_expiry_ret;

int32_t nrf_modem_gnss_agnss_expiry_get(struct nrf_modem_gnss_agnss_expiry *expiry)
{
	if (mock_expiry_ret)
		return mock_expiry_ret;
	*expiry = mock_expiry;
	return 0;
}

static void mock_reset(void)
{
	mock_write_count = 0;
	memset(mock_write_types, 0, sizeof(mock_write_types));
	memset(mock_write_sizes, 0, sizeof(mock_write_sizes));
	memset(&mock_expiry, 0, sizeof(mock_expiry));
	mock_expiry_ret = 0;
}

static struct gps_assist_data make_test_data(void)
{
	struct gps_assist_data d;

	memset(&d, 0, sizeof(d));

	d.gps_week = 2409;
	d.num_sv = 1;
	d.timestamp = 1773064800U;

	d.iono.alpha[0] = 1.1176e-08;
	d.iono.alpha[1] = 0.0;
	d.iono.alpha[2] = -5.9605e-08;
	d.iono.alpha[3] = 0.0;
	d.iono.beta[0]  = 9.0112e+04;
	d.iono.beta[1]  = 0.0;
	d.iono.beta[2]  = -1.9661e+05;
	d.iono.beta[3]  = 0.0;

	d.utc.a0    = 1.862645149e-09;
	d.utc.a1    = 4.44089209900e-15;
	d.utc.tot   = 233472;
	d.utc.wnt   = 2409;
	d.utc.dt_ls = 18;

	struct gps_ephemeris *sv = &d.sv[0];
	sv->prn       = 1;
	sv->health    = 0;
	sv->iodc      = 933;
	sv->iode      = 73;
	sv->week      = 2409;
	sv->toe       = 86400;
	sv->toc       = 86400;
	sv->af0       = -3.459286689758e-04;
	sv->af1       = -1.023181539495e-11;
	sv->af2       = 0.0;
	sv->sqrt_a    = 5.153622903824e+03;
	sv->e         = 1.603523269296e-03;
	sv->i0        = 9.581923839646e-01;
	sv->omega0    = 3.011965235162e+00;
	sv->omega     = 9.957733874442e-02;
	sv->m0        = -1.700227616185e+00;
	sv->delta_n   = 4.197729910498e-09;
	sv->omega_dot = -8.327132572782e-09;
	sv->idot      = -6.500270762369e-11;
	sv->cuc       = -6.884336471558e-06;
	sv->cus       = 3.907829523087e-06;
	sv->crc       = 303.59375;
	sv->crs       = -131.5625;
	sv->cic       = 5.587935447693e-09;
	sv->cis       = -2.980232238770e-08;
	sv->tgd       = -9.313225746155e-09;

	return d;
}

static void test_inject_call_count(void)
{
	struct gps_assist_data d = make_test_data();

	TEST("inject: correct number of agnss_write calls");
	mock_reset();
	int rc = gps_assist_inject(&d);
	ASSERT_INT_EQ(rc, 0);
	/* 1 eph + 1 alm + 1 iono + 1 utc + 1 system_time = 5 calls
	 * (no location since location.valid == 0)
	 */
	ASSERT_INT_EQ(mock_write_count, 5);
	PASS();
	return;
out:;
}

static void test_inject_call_types(void)
{
	struct gps_assist_data d = make_test_data();

	TEST("inject: correct agnss_write type constants");
	mock_reset();
	gps_assist_inject(&d);
	ASSERT_INT_EQ(mock_write_types[0], NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES);
	ASSERT_INT_EQ(mock_write_types[1], NRF_MODEM_GNSS_AGNSS_GPS_ALMANAC);
	ASSERT_INT_EQ(mock_write_types[2], NRF_MODEM_GNSS_AGNSS_KLOBUCHAR_IONOSPHERIC_CORRECTION);
	ASSERT_INT_EQ(mock_write_types[3], NRF_MODEM_GNSS_AGNSS_GPS_UTC_PARAMETERS);
	ASSERT_INT_EQ(mock_write_types[4], NRF_MODEM_GNSS_AGNSS_GPS_SYSTEM_CLOCK_AND_TOWS);
	PASS();
	return;
out:;
}

static void test_inject_call_sizes(void)
{
	struct gps_assist_data d = make_test_data();

	TEST("inject: correct buffer sizes");
	mock_reset();
	gps_assist_inject(&d);
	ASSERT_INT_EQ(mock_write_sizes[0], (int)sizeof(struct nrf_modem_gnss_agnss_gps_data_ephemeris));
	ASSERT_INT_EQ(mock_write_sizes[1], (int)sizeof(struct nrf_modem_gnss_agnss_gps_data_almanac));
	ASSERT_INT_EQ(mock_write_sizes[2], (int)sizeof(struct nrf_modem_gnss_agnss_data_klobuchar));
	ASSERT_INT_EQ(mock_write_sizes[3], (int)sizeof(struct nrf_modem_gnss_agnss_gps_data_utc));
	ASSERT_INT_EQ(mock_write_sizes[4], (int)sizeof(struct nrf_modem_gnss_agnss_gps_data_system_time_and_sv_tow));
	PASS();
	return;
out:;
}

static void test_convert_time(void)
{
	struct gps_assist_data d = make_test_data();

	TEST("convert: toe and toc scaling (2^4 = 16)");

	uint16_t toc_icd = (uint16_t)(d.sv[0].toc / 16);
	uint16_t toe_icd = (uint16_t)(d.sv[0].toe / 16);
	ASSERT_INT_EQ(toc_icd, 5400);
	ASSERT_INT_EQ(toe_icd, 5400);
	ASSERT_INT_EQ(toc_icd * 16, 86400);
	ASSERT_INT_EQ(toe_icd * 16, 86400);
	PASS();
	return;
out:;
}

static void test_convert_clock_roundtrip(void)
{
	struct gps_assist_data d = make_test_data();
	double af0 = d.sv[0].af0;
	double af1 = d.sv[0].af1;

	TEST("convert: af0/af1 round-trip within 1 LSB");

	/* af0: scale = 2^-31 */
	double lsb_af0 = 9.31322574615478515625e-10;
	int32_t af0_icd = (int32_t)round(af0 / lsb_af0);
	double af0_back = af0_icd * lsb_af0;
	ASSERT_DBL_NEAR(af0_back, af0, lsb_af0);

	/* af1: scale = 2^-43 */
	double lsb_af1 = 1.13686837721616029739e-13;
	int16_t af1_icd = (int16_t)round(af1 / lsb_af1);
	double af1_back = af1_icd * lsb_af1;
	ASSERT_DBL_NEAR(af1_back, af1, lsb_af1);

	PASS();
	return;
out:;
}

static void test_convert_m0_roundtrip(void)
{
	struct gps_assist_data d = make_test_data();
	double m0_rad = d.sv[0].m0;

	TEST("convert: M0 rad -> semi-circles -> ICD round-trip");

	double lsb = 4.65661287307739257812e-10;  /* 2^-31 semi-circles */
	double m0_sc = m0_rad / M_PI;
	int32_t m0_icd = (int32_t)round(m0_sc / lsb);
	double m0_sc_back = m0_icd * lsb;
	double m0_rad_back = m0_sc_back * M_PI;

	ASSERT_DBL_NEAR(m0_rad_back, m0_rad, lsb * M_PI);
	PASS();
	return;
out:;
}

static void test_convert_eccentricity_roundtrip(void)
{
	struct gps_assist_data d = make_test_data();
	double e = d.sv[0].e;

	TEST("convert: eccentricity round-trip within 1 LSB");

	double lsb = 1.16415321826934814453e-10;  /* 2^-33 */
	uint32_t e_icd = (uint32_t)round(e / lsb);
	double e_back = e_icd * lsb;
	ASSERT_DBL_NEAR(e_back, e, lsb);
	PASS();
	return;
out:;
}

static void test_convert_sqrt_a_roundtrip(void)
{
	struct gps_assist_data d = make_test_data();
	double sqrt_a = d.sv[0].sqrt_a;

	TEST("convert: sqrt_A round-trip within 1 LSB");

	double lsb = 1.90734863281250000000e-06;  /* 2^-19 */
	uint32_t sqrt_a_icd = (uint32_t)round(sqrt_a / lsb);
	double sqrt_a_back = sqrt_a_icd * lsb;
	ASSERT_DBL_NEAR(sqrt_a_back, sqrt_a, lsb);
	PASS();
	return;
out:;
}

static void test_convert_harmonic_roundtrip(void)
{
	struct gps_assist_data d = make_test_data();

	TEST("convert: Crc/Crs (2^-5 m) round-trip");

	double lsb_m = 3.12500000000000000000e-02;  /* 2^-5 */
	int16_t crc_icd = (int16_t)round(d.sv[0].crc / lsb_m);
	double crc_back = crc_icd * lsb_m;
	ASSERT_DBL_NEAR(crc_back, d.sv[0].crc, lsb_m);

	int16_t crs_icd = (int16_t)round(d.sv[0].crs / lsb_m);
	double crs_back = crs_icd * lsb_m;
	ASSERT_DBL_NEAR(crs_back, d.sv[0].crs, lsb_m);

	PASS();
	return;
out:;
}

static void test_convert_iono_roundtrip(void)
{
	struct gps_assist_data d = make_test_data();

	TEST("convert: Klobuchar iono alpha/beta round-trip");

	double lsb_a0 = 9.31322574615478515625e-10;
	int8_t a0_icd = (int8_t)round(d.iono.alpha[0] / lsb_a0);
	double a0_back = a0_icd * lsb_a0;
	ASSERT_DBL_NEAR(a0_back, d.iono.alpha[0], lsb_a0);

	double lsb_b0 = 2048.0;
	int8_t b0_icd = (int8_t)round(d.iono.beta[0] / lsb_b0);
	double b0_back = b0_icd * lsb_b0;
	ASSERT_DBL_NEAR(b0_back, d.iono.beta[0], lsb_b0);

	PASS();
	return;
out:;
}

static void test_convert_utc_roundtrip(void)
{
	struct gps_assist_data d = make_test_data();

	TEST("convert: UTC A0/A1/tot round-trip");

	double lsb_a0 = 9.31322574615478515625e-10;
	int32_t a0_icd = (int32_t)round(d.utc.a0 / lsb_a0);
	double a0_back = a0_icd * lsb_a0;
	ASSERT_DBL_NEAR(a0_back, d.utc.a0, lsb_a0);

	uint8_t tot_icd = (uint8_t)(d.utc.tot / 4096);
	ASSERT_INT_EQ(tot_icd * 4096, (int)d.utc.tot);

	ASSERT_INT_EQ(d.utc.dt_ls, 18);

	PASS();
	return;
out:;
}

static void test_inject_multi_sv(void)
{
	struct gps_assist_data d = make_test_data();

	d.num_sv = 2;
	d.sv[1] = d.sv[0];
	d.sv[1].prn = 15;

	TEST("inject: 2 SVs -> 7 agnss_write calls");
	mock_reset();
	int rc = gps_assist_inject(&d);
	ASSERT_INT_EQ(rc, 0);
	/* 2*(eph+alm) + 1 iono + 1 utc + 1 system_time = 7 */
	ASSERT_INT_EQ(mock_write_count, 7);
	ASSERT_INT_EQ(mock_write_types[0], NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES);
	ASSERT_INT_EQ(mock_write_types[1], NRF_MODEM_GNSS_AGNSS_GPS_ALMANAC);
	ASSERT_INT_EQ(mock_write_types[2], NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES);
	ASSERT_INT_EQ(mock_write_types[3], NRF_MODEM_GNSS_AGNSS_GPS_ALMANAC);
	ASSERT_INT_EQ(mock_write_types[4], NRF_MODEM_GNSS_AGNSS_KLOBUCHAR_IONOSPHERIC_CORRECTION);
	ASSERT_INT_EQ(mock_write_types[5], NRF_MODEM_GNSS_AGNSS_GPS_UTC_PARAMETERS);
	ASSERT_INT_EQ(mock_write_types[6], NRF_MODEM_GNSS_AGNSS_GPS_SYSTEM_CLOCK_AND_TOWS);
	PASS();
	return;
out:;
}

static void test_convert_system_time(void)
{
	struct gps_assist_data d = make_test_data();

	TEST("convert: system time from unix timestamp");

	long gps_t = (long)(d.timestamp - 315964800L) + 18;
	uint16_t expected_days = (uint16_t)(gps_t / 86400);
	uint32_t expected_tod  = (uint32_t)(gps_t % 86400);

	ASSERT_TRUE(expected_days > 16000);
	ASSERT_TRUE(expected_tod < 86400);
	/* Round-trip: days*86400 + tod + epoch - leap = original timestamp */
	long reconstructed = (long)expected_days * 86400 + expected_tod
			   + 315964800L - 18;
	ASSERT_INT_EQ((int32_t)reconstructed, (int32_t)d.timestamp);

	PASS();
	return;
out:;
}

static void test_inject_with_location(void)
{
	struct gps_assist_data d = make_test_data();

	d.location.latitude  = 48.8566;
	d.location.longitude = 2.3522;
	d.location.altitude  = 35;
	d.location.valid     = 1;

	TEST("inject: with location -> 6 agnss_write calls");
	mock_reset();
	int rc = gps_assist_inject(&d);
	ASSERT_INT_EQ(rc, 0);
	/* 1 eph + 1 alm + 1 iono + 1 utc + 1 systime + 1 location = 6 */
	ASSERT_INT_EQ(mock_write_count, 6);
	ASSERT_INT_EQ(mock_write_types[5], NRF_MODEM_GNSS_AGNSS_LOCATION);
	ASSERT_INT_EQ(mock_write_sizes[5], (int)sizeof(struct nrf_modem_gnss_agnss_data_location));
	PASS();
	return;
out:;
}

static void test_convert_location(void)
{
	struct gps_assist_data d = make_test_data();

	d.location.latitude  = 48.8566;
	d.location.longitude = 2.3522;
	d.location.altitude  = 35;
	d.location.valid     = 1;

	TEST("convert: location lat/lon encoding");

	/*
	 * latitude coded:  N = (2^23 / 90) * 48.8566 = ~4549098
	 * longitude coded: N = (2^24 / 360) * 2.3522  = ~109697
	 */
	int32_t expected_lat = (int32_t)round(48.8566 * (double)(1 << 23) / 90.0);
	int32_t expected_lon = (int32_t)round(2.3522 * (double)(1 << 24) / 360.0);

	ASSERT_TRUE(expected_lat > 4500000 && expected_lat < 4600000);
	ASSERT_TRUE(expected_lon > 100000 && expected_lon < 120000);

	mock_reset();
	gps_assist_inject_location(&d);
	ASSERT_INT_EQ(mock_write_count, 1);
	ASSERT_INT_EQ(mock_write_types[0], NRF_MODEM_GNSS_AGNSS_LOCATION);

	PASS();
	return;
out:;
}

static void test_selective_inject_utc_only(void)
{
	struct gps_assist_data d = make_test_data();
	struct nrf_modem_gnss_agnss_data_frame req;

	memset(&req, 0, sizeof(req));
	req.data_flags = NRF_MODEM_GNSS_AGNSS_GPS_UTC_REQUEST;
	req.system_count = 0;

	TEST("selective: UTC only -> 1 call");
	mock_reset();
	int rc = gps_assist_inject_from_request(&d, &req);
	ASSERT_INT_EQ(rc, 0);
	ASSERT_INT_EQ(mock_write_count, 1);
	ASSERT_INT_EQ(mock_write_types[0], NRF_MODEM_GNSS_AGNSS_GPS_UTC_PARAMETERS);
	PASS();
	return;
out:;
}

static void test_selective_inject_eph_mask(void)
{
	struct gps_assist_data d = make_test_data();

	d.num_sv = 2;
	d.sv[1] = d.sv[0];
	d.sv[1].prn = 15;

	struct nrf_modem_gnss_agnss_data_frame req;

	memset(&req, 0, sizeof(req));
	req.system_count = 1;
	req.system[0].system_id = NRF_MODEM_GNSS_SYSTEM_GPS;
	/* Request only PRN 15 (bit 14) */
	req.system[0].sv_mask_ephe = (uint64_t)1 << 14;

	TEST("selective: 1 of 2 ephe by mask -> 1 call");
	mock_reset();
	int rc = gps_assist_inject_from_request(&d, &req);
	ASSERT_INT_EQ(rc, 0);
	ASSERT_INT_EQ(mock_write_count, 1);
	ASSERT_INT_EQ(mock_write_types[0], NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES);
	PASS();
	return;
out:;
}

static void test_selective_inject_all_flags(void)
{
	struct gps_assist_data d = make_test_data();

	d.location.latitude  = 48.8566;
	d.location.longitude = 2.3522;
	d.location.valid     = 1;

	struct nrf_modem_gnss_agnss_data_frame req;

	memset(&req, 0, sizeof(req));
	req.data_flags = NRF_MODEM_GNSS_AGNSS_GPS_UTC_REQUEST
		       | NRF_MODEM_GNSS_AGNSS_KLOBUCHAR_REQUEST
		       | NRF_MODEM_GNSS_AGNSS_GPS_SYS_TIME_AND_SV_TOW_REQUEST
		       | NRF_MODEM_GNSS_AGNSS_POSITION_REQUEST;
	req.system_count = 1;
	req.system[0].system_id = NRF_MODEM_GNSS_SYSTEM_GPS;
	req.system[0].sv_mask_ephe = (uint64_t)1 << 0;  /* PRN 1 */

	TEST("selective: all flags + 1 eph -> 5 calls");
	mock_reset();
	int rc = gps_assist_inject_from_request(&d, &req);
	ASSERT_INT_EQ(rc, 0);
	ASSERT_INT_EQ(mock_write_count, 5);
	ASSERT_INT_EQ(mock_write_types[0], NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES);
	ASSERT_INT_EQ(mock_write_types[1], NRF_MODEM_GNSS_AGNSS_GPS_UTC_PARAMETERS);
	ASSERT_INT_EQ(mock_write_types[2], NRF_MODEM_GNSS_AGNSS_KLOBUCHAR_IONOSPHERIC_CORRECTION);
	ASSERT_INT_EQ(mock_write_types[3], NRF_MODEM_GNSS_AGNSS_GPS_SYSTEM_CLOCK_AND_TOWS);
	ASSERT_INT_EQ(mock_write_types[4], NRF_MODEM_GNSS_AGNSS_LOCATION);
	PASS();
	return;
out:;
}

static void test_expiry_nothing_expired(void)
{
	struct gps_assist_data d = make_test_data();

	TEST("expiry: nothing expired -> 0 writes");
	mock_reset();

	/* All expiry times > 0, no data_flags -> nothing to do */
	mock_expiry.data_flags = 0;
	mock_expiry.utc_expiry = 120;
	mock_expiry.klob_expiry = 120;
	mock_expiry.sv_count = 1;
	mock_expiry.sv[0].sv_id = 1;
	mock_expiry.sv[0].system_id = NRF_MODEM_GNSS_SYSTEM_GPS;
	mock_expiry.sv[0].ephe_expiry = 60;  /* 60 min remaining */
	mock_expiry.sv[0].alm_expiry  = 60;

	int rc = gps_assist_check_expiry(&d);
	ASSERT_INT_EQ(rc, 0);
	ASSERT_INT_EQ(mock_write_count, 0);
	PASS();
	return;
out:;
}

static void test_expiry_utc_and_eph(void)
{
	struct gps_assist_data d = make_test_data();

	d.num_sv = 2;
	d.sv[1] = d.sv[0];
	d.sv[1].prn = 15;

	TEST("expiry: UTC + 1 eph expired -> 2 writes");
	mock_reset();

	/* UTC expired via data_flags, PRN 15 ephemeris expired */
	mock_expiry.data_flags = NRF_MODEM_GNSS_AGNSS_GPS_UTC_REQUEST;
	mock_expiry.sv_count = 2;
	mock_expiry.sv[0].sv_id = 1;
	mock_expiry.sv[0].system_id = NRF_MODEM_GNSS_SYSTEM_GPS;
	mock_expiry.sv[0].ephe_expiry = 30;   /* still valid */
	mock_expiry.sv[0].alm_expiry  = 30;
	mock_expiry.sv[1].sv_id = 15;
	mock_expiry.sv[1].system_id = NRF_MODEM_GNSS_SYSTEM_GPS;
	mock_expiry.sv[1].ephe_expiry = 0;    /* expired */
	mock_expiry.sv[1].alm_expiry  = 30;   /* still valid */

	int rc = gps_assist_check_expiry(&d);
	ASSERT_INT_EQ(rc, 0);
	ASSERT_INT_EQ(mock_write_count, 2);
	ASSERT_INT_EQ(mock_write_types[0], NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES);
	ASSERT_INT_EQ(mock_write_types[1], NRF_MODEM_GNSS_AGNSS_GPS_UTC_PARAMETERS);
	PASS();
	return;
out:;
}

static void test_expiry_get_failure(void)
{
	struct gps_assist_data d = make_test_data();

	TEST("expiry: expiry_get fails -> propagate error");
	mock_reset();
	mock_expiry_ret = -1;

	int rc = gps_assist_check_expiry(&d);
	ASSERT_INT_EQ(rc, -1);
	ASSERT_INT_EQ(mock_write_count, 0);
	PASS();
	return;
out:;
}

static void test_convert_almanac_roundtrip(void)
{
	struct gps_assist_data d = make_test_data();

	TEST("convert: almanac derived from ephemeris round-trip");

	double e_orig = d.sv[0].e;
	double sqrt_a_orig = d.sv[0].sqrt_a;
	double m0_sc = d.sv[0].m0 / M_PI;
	double omega0_sc = d.sv[0].omega0 / M_PI;

	/* e: 2^-21 */
	double lsb_e = 4.76837158203125e-07;
	uint16_t e_alm = (uint16_t)round(e_orig / lsb_e);
	double e_back = e_alm * lsb_e;
	ASSERT_DBL_NEAR(e_back, e_orig, lsb_e);

	/* sqrt_a: 2^-11 */
	double lsb_sqrt_a = 4.88281250000000000000e-04;
	uint32_t sqrt_a_alm = (uint32_t)round(sqrt_a_orig / lsb_sqrt_a);
	double sqrt_a_back = sqrt_a_alm * lsb_sqrt_a;
	ASSERT_DBL_NEAR(sqrt_a_back, sqrt_a_orig, lsb_sqrt_a);

	/* m0: 2^-23 sc */
	double lsb_ang = 1.19209289550781250000e-07;
	int32_t m0_alm = (int32_t)round(m0_sc / lsb_ang);
	double m0_back = m0_alm * lsb_ang;
	ASSERT_DBL_NEAR(m0_back, m0_sc, lsb_ang);

	/* omega0: 2^-23 sc */
	int32_t omega0_alm = (int32_t)round(omega0_sc / lsb_ang);
	double omega0_back = omega0_alm * lsb_ang;
	ASSERT_DBL_NEAR(omega0_back, omega0_sc, lsb_ang);

	PASS();
	return;
out:;
}

static void test_selective_inject_alm_mask(void)
{
	struct gps_assist_data d = make_test_data();

	d.num_sv = 2;
	d.sv[1] = d.sv[0];
	d.sv[1].prn = 15;

	struct nrf_modem_gnss_agnss_data_frame req;

	memset(&req, 0, sizeof(req));
	req.system_count = 1;
	req.system[0].system_id = NRF_MODEM_GNSS_SYSTEM_GPS;
	/* Request almanac for PRN 1 (bit 0) and ephemeris for PRN 15 (bit 14) */
	req.system[0].sv_mask_alm  = (uint64_t)1 << 0;
	req.system[0].sv_mask_ephe = (uint64_t)1 << 14;

	TEST("selective: 1 alm + 1 eph by mask -> 2 calls");
	mock_reset();
	int rc = gps_assist_inject_from_request(&d, &req);
	ASSERT_INT_EQ(rc, 0);
	ASSERT_INT_EQ(mock_write_count, 2);
	/* PRN 1 eph not requested, PRN 1 alm first, then PRN 15 eph */
	ASSERT_INT_EQ(mock_write_types[0], NRF_MODEM_GNSS_AGNSS_GPS_ALMANAC);
	ASSERT_INT_EQ(mock_write_types[1], NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES);
	PASS();
	return;
out:;
}

static void test_expiry_alm_expired(void)
{
	struct gps_assist_data d = make_test_data();

	TEST("expiry: almanac expired -> 1 almanac write");
	mock_reset();

	mock_expiry.data_flags = 0;
	mock_expiry.sv_count = 1;
	mock_expiry.sv[0].sv_id = 1;
	mock_expiry.sv[0].system_id = NRF_MODEM_GNSS_SYSTEM_GPS;
	mock_expiry.sv[0].ephe_expiry = 60;  /* still valid */
	mock_expiry.sv[0].alm_expiry  = 0;   /* expired */

	int rc = gps_assist_check_expiry(&d);
	ASSERT_INT_EQ(rc, 0);
	ASSERT_INT_EQ(mock_write_count, 1);
	ASSERT_INT_EQ(mock_write_types[0], NRF_MODEM_GNSS_AGNSS_GPS_ALMANAC);
	PASS();
	return;
out:;
}

int main(void)
{
	fprintf(stderr, "=== nRF conversion tests ===\n");

	test_inject_call_count();
	test_inject_call_types();
	test_inject_call_sizes();
	test_convert_time();
	test_convert_clock_roundtrip();
	test_convert_m0_roundtrip();
	test_convert_eccentricity_roundtrip();
	test_convert_sqrt_a_roundtrip();
	test_convert_harmonic_roundtrip();
	test_convert_iono_roundtrip();
	test_convert_utc_roundtrip();
	test_inject_multi_sv();
	test_inject_with_location();
	test_convert_system_time();
	test_convert_location();
	test_convert_almanac_roundtrip();
	test_selective_inject_utc_only();
	test_selective_inject_eph_mask();
	test_selective_inject_alm_mask();
	test_selective_inject_all_flags();
	test_expiry_nothing_expired();
	test_expiry_utc_and_eph();
	test_expiry_alm_expired();
	test_expiry_get_failure();

	fprintf(stderr, "\n%d/%d tests passed\n", tests_passed, tests_run);
	return tests_passed == tests_run ? 0 : 1;
}
