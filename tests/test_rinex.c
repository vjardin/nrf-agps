/*
 * test_rinex.c - RINEX parser tests
 *
 * Tests the RINEX v3 navigation file parser with an embedded fixture
 * (no network required) and optionally a live download integration test.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#include "gps_assist.h"
#include "rinex.h"

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

/*
 * Minimal RINEX v3.04 navigation file fixture.
 * Contains one GPS satellite (G01), one QZSS satellite (J01),
 * and one GLONASS satellite (R01, skipped).
 */
/* Each header line: 60 chars data + 20 chars label = 80 columns */
static const char rinex_fixture[] =
"     3.04           N: GNSS NAV DATA    M: Mixed            RINEX VERSION / TYPE\n"
"test                 test                20260309 000000 UTCPGM / RUN BY / DATE \n"
"GPSA   1.1176D-08  0.0000D+00 -5.9605D-08  0.0000D+00       IONOSPHERIC CORR    \n"
"GPSB   9.0112D+04  0.0000D+00 -1.9661D+05  0.0000D+00       IONOSPHERIC CORR    \n"
"GPUT  0.1862645149D-08 0.444089209900D-14   233472 2409     TIME SYSTEM CORR    \n"
"    18    18  2413     7                                    LEAP SECONDS        \n"
"                                                            END OF HEADER       \n"
"G01 2026 03 09 00 00 00-3.459286689758D-04-1.023181539495D-11 0.000000000000D+00\n"
"     7.300000000000D+01-1.315625000000D+02 4.197729910498D-09-1.700227616185D+00\n"
"    -6.884336471558D-06 1.603523269296D-03 3.907829523087D-06 5.153622903824D+03\n"
"     8.640000000000D+04 5.587935447693D-09 3.011965235162D+00-2.980232238770D-08\n"
"     9.581923839646D-01 3.035937500000D+02 9.957733874442D-02-8.327132572782D-09\n"
"    -6.500270762369D-11 1.000000000000D+00 2.409000000000D+03 0.000000000000D+00\n"
"     2.000000000000D+00 0.000000000000D+00-9.313225746155D-09 9.330000000000D+02\n"
"     7.948800000000D+04 4.000000000000D+00 0.000000000000D+00 0.000000000000D+00\n"
"J01 2026 03 09 02 00 00 1.127189025283D-04 2.273736754432D-12 0.000000000000D+00\n"
"     5.700000000000D+01 2.234375000000D+02 2.862944186498D-09 2.710635288216D+00\n"
"     1.068785786629D-05 7.548052072525D-02 7.372349500656D-06 6.493282413483D+03\n"
"     9.360000000000D+04-3.725290298462D-09 1.448671913564D+00-1.117587089539D-08\n"
"     7.200032782573D-01 2.171875000000D+01 1.099015085551D+00-3.485049934832D-09\n"
"     1.064907174953D-10 1.000000000000D+00 2.409000000000D+03 0.000000000000D+00\n"
"     2.800000000000D+00 0.000000000000D+00-4.656612873077D-09 5.700000000000D+01\n"
"     8.640000000000D+04 4.000000000000D+00 0.000000000000D+00 0.000000000000D+00\n"
"R01 2026 03 09 00 15 00 2.365745604038D-04-2.728484105319D-12 0.000000000000D+00\n"
"     1.000000000000D+00-1.018750000000D+01 3.063621520996D-09 0.000000000000D+00\n"
"     2.041721191406D+04 2.178478240967D+00-1.218914985657D+03 0.000000000000D+00\n"
"     9.020766601563D+03-1.523220062256D+00 1.734428100586D+04 0.000000000000D+00\n";

static const char *fixture_path;

static void setup_fixture(void)
{
	static char path[] = "/tmp/test_rinex_XXXXXX";
	int fd = mkstemp(path);

	assert(fd >= 0);
	ssize_t n = write(fd, rinex_fixture, strlen(rinex_fixture));
	assert(n == (ssize_t)strlen(rinex_fixture));
	close(fd);
	fixture_path = path;
}

static void cleanup_fixture(void)
{
	if (fixture_path)
		remove(fixture_path);
}

static void test_parse_basic(void)
{
	struct gps_assist_data data;

	TEST("parse fixture: returns success");
	int rc = rinex_parse(fixture_path, &data);
	ASSERT_INT_EQ(rc, 0);
	PASS();
	return;
out:;
}

static void test_parse_satellite_count(void)
{
	struct gps_assist_data data;
	rinex_parse(fixture_path, &data);

	TEST("parse fixture: 1 GPS satellite (GLONASS skipped)");
	ASSERT_INT_EQ(data.num_sv, 1);
	PASS();
	return;
out:;
}

static void test_parse_prn(void)
{
	struct gps_assist_data data;
	rinex_parse(fixture_path, &data);

	TEST("parse fixture: PRN = 1");
	ASSERT_INT_EQ(data.sv[0].prn, 1);
	PASS();
	return;
out:;
}

static void test_parse_week(void)
{
	struct gps_assist_data data;
	rinex_parse(fixture_path, &data);

	TEST("parse fixture: GPS week = 2409");
	ASSERT_INT_EQ(data.sv[0].week, 2409);
	ASSERT_INT_EQ(data.gps_week, 2409);
	PASS();
	return;
out:;
}

static void test_parse_clock(void)
{
	struct gps_assist_data data;
	rinex_parse(fixture_path, &data);

	TEST("parse fixture: clock parameters");
	ASSERT_DBL_NEAR(data.sv[0].af0, -3.459286689758e-04, 1e-16);
	ASSERT_DBL_NEAR(data.sv[0].af1, -1.023181539495e-11, 1e-23);
	ASSERT_DBL_NEAR(data.sv[0].af2, 0.0, 1e-30);
	PASS();
	return;
out:;
}

static void test_parse_orbit(void)
{
	struct gps_assist_data data;
	rinex_parse(fixture_path, &data);

	TEST("parse fixture: orbital parameters");
	/* Orbit line 0: IODE=73, Crs=-131.5625, delta_n, M0 */
	ASSERT_INT_EQ(data.sv[0].iode, 73);
	ASSERT_DBL_NEAR(data.sv[0].crs, -131.5625, 1e-4);
	ASSERT_DBL_NEAR(data.sv[0].delta_n, 4.197729910498e-09, 1e-21);
	ASSERT_DBL_NEAR(data.sv[0].m0, -1.700227616185e+00, 1e-12);
	/* Orbit line 1: Cuc, e, Cus, sqrt_A */
	ASSERT_DBL_NEAR(data.sv[0].e, 1.603523269296e-03, 1e-15);
	ASSERT_DBL_NEAR(data.sv[0].sqrt_a, 5.153622903824e+03, 1e-8);
	/* Orbit line 2: toe, Cic, omega0, Cis */
	ASSERT_INT_EQ(data.sv[0].toe, 86400);
	ASSERT_DBL_NEAR(data.sv[0].omega0, 3.011965235162e+00, 1e-12);
	/* Orbit line 3: i0, Crc, omega, omega_dot */
	ASSERT_DBL_NEAR(data.sv[0].i0, 9.581923839646e-01, 1e-12);
	ASSERT_DBL_NEAR(data.sv[0].crc, 303.59375, 1e-4);
	ASSERT_DBL_NEAR(data.sv[0].omega, 9.957733874442e-02, 1e-13);
	ASSERT_DBL_NEAR(data.sv[0].omega_dot, -8.327132572782e-09, 1e-21);
	/* Orbit line 4: IDOT, _, week, _ */
	ASSERT_DBL_NEAR(data.sv[0].idot, -6.500270762369e-11, 1e-23);
	/* Orbit line 5: _, health, TGD, IODC */
	ASSERT_INT_EQ(data.sv[0].health, 0);
	ASSERT_DBL_NEAR(data.sv[0].tgd, -9.313225746155e-09, 1e-21);
	ASSERT_INT_EQ(data.sv[0].iodc, 933);
	PASS();
	return;
out:;
}

static void test_parse_iono(void)
{
	struct gps_assist_data data;
	rinex_parse(fixture_path, &data);

	TEST("parse fixture: ionospheric alpha/beta");
	ASSERT_DBL_NEAR(data.iono.alpha[0], 1.1176e-08, 1e-12);
	ASSERT_DBL_NEAR(data.iono.alpha[1], 0.0, 1e-20);
	ASSERT_DBL_NEAR(data.iono.alpha[2], -5.9605e-08, 1e-12);
	ASSERT_DBL_NEAR(data.iono.beta[0], 9.0112e+04, 1e-1);
	ASSERT_DBL_NEAR(data.iono.beta[2], -1.9661e+05, 1e+0);
	PASS();
	return;
out:;
}

static void test_parse_utc(void)
{
	struct gps_assist_data data;
	rinex_parse(fixture_path, &data);

	TEST("parse fixture: UTC parameters");
	ASSERT_DBL_NEAR(data.utc.a0, 1.862645149e-09, 1e-18);
	ASSERT_DBL_NEAR(data.utc.a1, 4.44089209900e-15, 1e-25);
	ASSERT_INT_EQ(data.utc.tot, 233472);
	ASSERT_INT_EQ(data.utc.wnt, 2409);
	ASSERT_INT_EQ(data.utc.dt_ls, 18);
	PASS();
	return;
out:;
}

static void test_parse_toc(void)
{
	struct gps_assist_data data;
	rinex_parse(fixture_path, &data);

	/*
	 * Epoch: 2026-03-09 00:00:00 UTC = GPS week 2409, day 1 (Monday)
	 * toc should be 86400 * 1 = 86400 (Monday 00:00:00 in GPS week
	 * seconds, where GPS week starts on Sunday).
	 *
	 * Actually: 2026-03-09 is a Monday.
	 * GPS week epoch = Sunday 2026-03-08 00:00:00.
	 * toc = 1 day = 86400 s.
	 */
	TEST("parse fixture: toc from epoch");
	ASSERT_INT_EQ(data.sv[0].toc, 86400);
	PASS();
	return;
out:;
}

static void test_parse_qzss_count(void)
{
	struct gps_assist_data data;
	rinex_parse(fixture_path, &data);

	TEST("parse fixture: 1 QZSS satellite");
	ASSERT_INT_EQ(data.num_qzss, 1);
	PASS();
	return;
out:;
}

static void test_parse_qzss_prn(void)
{
	struct gps_assist_data data;
	rinex_parse(fixture_path, &data);

	TEST("parse fixture: QZSS PRN = 193 (J01)");
	ASSERT_INT_EQ(data.qzss[0].prn, 193);
	PASS();
	return;
out:;
}

static void test_parse_qzss_orbit(void)
{
	struct gps_assist_data data;
	rinex_parse(fixture_path, &data);

	TEST("parse fixture: QZSS orbital parameters");
	/* QZSS has higher eccentricity and different sqrt_a than GPS */
	ASSERT_DBL_NEAR(data.qzss[0].e, 7.548052072525e-02, 1e-14);
	ASSERT_DBL_NEAR(data.qzss[0].sqrt_a, 6.493282413483e+03, 1e-8);
	ASSERT_INT_EQ(data.qzss[0].toe, 93600);
	ASSERT_INT_EQ(data.qzss[0].week, 2409);
	ASSERT_DBL_NEAR(data.qzss[0].af0, 1.127189025283e-04, 1e-16);
	/* GPS satellite count should be unaffected */
	ASSERT_INT_EQ(data.num_sv, 1);
	PASS();
	return;
out:;
}

static void test_download_and_parse(void)
{
	struct gps_assist_data data;
	char *path;

	TEST("integration: download + parse recent BRDC");

	/*
	 * Try yesterday first, then day-before-yesterday as fallback.
	 * BKG's combined BRDC file may temporarily lack GPS records
	 * (it is built incrementally throughout the day).
	 */
	int rc = -1;
	for (int day_offset = 1; day_offset <= 2 && rc != 0; day_offset++) {
		time_t t = time(NULL) - day_offset * 86400;
		struct tm *tm = gmtime(&t);
		int year = tm->tm_year + 1900;
		int yday = tm->tm_yday + 1;

		fprintf(stderr, "  trying day-%d (DOY %03d)...\n",
			day_offset, yday);

		for (int attempt = 1; attempt <= 3; attempt++) {
			path = rinex_download(year, yday, NULL);
			if (path) {
				memset(&data, 0, sizeof(data));
				rc = rinex_parse(path, &data);
				if (rc == 0 && data.num_sv >= 24)
					break;
				fprintf(stderr,
					"  attempt %d/3: got %d GPS SVs"
					" (need >=24)\n",
					attempt, data.num_sv);
				if (attempt < 3) {
					remove(path);
					free(path);
					path = NULL;
				}
			} else {
				fprintf(stderr,
					"  attempt %d/3: download failed\n",
					attempt);
			}
			if (attempt < 3) {
				fprintf(stderr, "  retrying in 3s...\n");
				sleep(3);
			}
		}

		/* Treat "parsed OK but insufficient GPS SVs" as failure
		 * for the day-fallback logic. */
		if (rc == 0 && data.num_sv < 24)
			rc = -1;

		/* If this day failed, clean up before trying next day */
		if (rc != 0 && day_offset < 2) {
			if (path) {
				remove(path);
				free(path);
				path = NULL;
			}
		}
	}
	if (rc != 0) {
		if (path)
			fprintf(stderr, "  keeping %s for debugging\n", path);
		FAIL("download/parse failed after all attempts");
		goto out;
	}
	/* Clean up on success */
	remove(path);
	free(path);
	path = NULL;
	ASSERT_TRUE(data.num_sv >= 24);
	ASSERT_TRUE(data.num_sv <= 32);
	ASSERT_TRUE(data.gps_week >= 2400);

	/* All satellites should have valid PRN and orbit */
	for (int i = 0; i < data.num_sv; i++) {
		ASSERT_TRUE(data.sv[i].prn >= 1 && data.sv[i].prn <= 32);
		ASSERT_TRUE(data.sv[i].sqrt_a > 5000.0);
		ASSERT_TRUE(data.sv[i].sqrt_a < 6000.0);
		ASSERT_TRUE(data.sv[i].e >= 0.0 && data.sv[i].e < 0.1);
		ASSERT_TRUE(data.sv[i].toe <= 604800);
	}

	/* Iono should be non-zero */
	ASSERT_TRUE(data.iono.alpha[0] != 0.0 || data.iono.alpha[1] != 0.0);
	ASSERT_TRUE(data.iono.beta[0] != 0.0 || data.iono.beta[1] != 0.0);

	/* UTC should be populated */
	ASSERT_TRUE(data.utc.tot > 0);
	ASSERT_TRUE(data.utc.dt_ls >= 18);

	PASS();
	return;
out:;
}

int main(int argc, char **argv)
{
	int integration = 0;

	if (argc > 1 && strcmp(argv[1], "--integration") == 0)
		integration = 1;

	fprintf(stderr, "=== RINEX parser tests ===\n");

	setup_fixture();

	test_parse_basic();
	test_parse_satellite_count();
	test_parse_prn();
	test_parse_week();
	test_parse_clock();
	test_parse_orbit();
	test_parse_iono();
	test_parse_utc();
	test_parse_toc();
	test_parse_qzss_count();
	test_parse_qzss_prn();
	test_parse_qzss_orbit();

	if (integration)
		test_download_and_parse();
	else
		fprintf(stderr, "  (skip integration test, use --integration)\n");

	cleanup_fixture();

	fprintf(stderr, "\n%d/%d tests passed\n", tests_passed, tests_run);
	return tests_passed == tests_run ? 0 : 1;
}
