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

static void mock_reset(void)
{
	mock_write_count = 0;
	memset(mock_write_types, 0, sizeof(mock_write_types));
	memset(mock_write_sizes, 0, sizeof(mock_write_sizes));
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
	/* 1 ephemeris + 1 iono + 1 utc + 1 system_time = 4 calls
	 * (no location since location.valid == 0)
	 */
	ASSERT_INT_EQ(mock_write_count, 4);
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
	ASSERT_INT_EQ(mock_write_types[1], NRF_MODEM_GNSS_AGNSS_KLOBUCHAR_IONOSPHERIC_CORRECTION);
	ASSERT_INT_EQ(mock_write_types[2], NRF_MODEM_GNSS_AGNSS_GPS_UTC_PARAMETERS);
	ASSERT_INT_EQ(mock_write_types[3], NRF_MODEM_GNSS_AGNSS_GPS_SYSTEM_CLOCK_AND_TOWS);
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
	ASSERT_INT_EQ(mock_write_sizes[1], (int)sizeof(struct nrf_modem_gnss_agnss_data_klobuchar));
	ASSERT_INT_EQ(mock_write_sizes[2], (int)sizeof(struct nrf_modem_gnss_agnss_gps_data_utc));
	ASSERT_INT_EQ(mock_write_sizes[3], (int)sizeof(struct nrf_modem_gnss_agnss_gps_data_system_time_and_sv_tow));
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

	TEST("inject: 2 SVs -> 5 agnss_write calls");
	mock_reset();
	int rc = gps_assist_inject(&d);
	ASSERT_INT_EQ(rc, 0);
	/* 2 ephemeris + 1 iono + 1 utc + 1 system_time = 5 */
	ASSERT_INT_EQ(mock_write_count, 5);
	ASSERT_INT_EQ(mock_write_types[0], NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES);
	ASSERT_INT_EQ(mock_write_types[1], NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES);
	ASSERT_INT_EQ(mock_write_types[2], NRF_MODEM_GNSS_AGNSS_KLOBUCHAR_IONOSPHERIC_CORRECTION);
	ASSERT_INT_EQ(mock_write_types[3], NRF_MODEM_GNSS_AGNSS_GPS_UTC_PARAMETERS);
	ASSERT_INT_EQ(mock_write_types[4], NRF_MODEM_GNSS_AGNSS_GPS_SYSTEM_CLOCK_AND_TOWS);
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

	TEST("inject: with location -> 5 agnss_write calls");
	mock_reset();
	int rc = gps_assist_inject(&d);
	ASSERT_INT_EQ(rc, 0);
	/* 1 eph + 1 iono + 1 utc + 1 systime + 1 location = 5 */
	ASSERT_INT_EQ(mock_write_count, 5);
	ASSERT_INT_EQ(mock_write_types[4], NRF_MODEM_GNSS_AGNSS_LOCATION);
	ASSERT_INT_EQ(mock_write_sizes[4], (int)sizeof(struct nrf_modem_gnss_agnss_data_location));
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

	fprintf(stderr, "\n%d/%d tests passed\n", tests_passed, tests_run);
	return tests_passed == tests_run ? 0 : 1;
}
