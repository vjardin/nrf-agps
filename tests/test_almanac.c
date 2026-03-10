/*
 * test_almanac.c - SEM and YUMA almanac parser tests
 *
 * Tests almanac_parse_sem() and almanac_parse_yuma() using embedded
 * fixture data written to temporary files.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "gps_assist.h"
#include "almanac.h"

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

/*
 * SEM fixture: 2 satellites with known values.
 * Angular values in semi-circles (native SEM format).
 */
static const char sem_fixture[] =
	" 2  TEST.AL3\n"
	" 2409 405504\n"
	"\n"
	"1\n"
	"63\n"
	"0\n"
	" 5.000000000000E-03  1.500000000000E-02 -8.000000000000E-09\n"
	" 5.153600000000E+03  9.500000000000E-01  5.000000000000E-02\n"
	"-2.500000000000E-01  1.000000000000E-04 -5.000000000000E-12\n"
	"0\n"
	"12\n"
	"\n"
	"15\n"
	"55\n"
	"0\n"
	" 1.000000000000E-02  2.000000000000E-02 -7.500000000000E-09\n"
	" 5.153500000000E+03 -3.000000000000E-01  8.000000000000E-02\n"
	" 4.000000000000E-01 -2.000000000000E-04  0.000000000000E+00\n"
	"0\n"
	"12\n";

/*
 * YUMA fixture: 1 satellite with values in radians.
 * Inclination = 1.0 rad -> delta_i = (1/pi) - 0.3 sc
 */
static const char yuma_fixture[] =
	"******** Week 2409 almanac for PRN-01 ********\n"
	"ID:                         01\n"
	"Health:                     000\n"
	"Eccentricity:               0.5000000000E-02\n"
	"Time of Applicability(s):   405504.0000\n"
	"Orbital Inclination(rad):   0.1000000000E+01\n"
	"Rate of Right Ascen(r/s):  -0.1000000000E-07\n"
	"SQRT(A)  (m 1/2):           5153.600000000\n"
	"Right Ascen at Week(rad):   0.2000000000E+01\n"
	"Argument of Perigee(rad):   0.5000000000E+00\n"
	"Mean Anom(rad):             -0.1000000000E+01\n"
	"Af0(s):                     0.1000000000E-03\n"
	"Af1(s/s):                  -0.5000000000E-11\n"
	"week:                       2409\n";

static int write_fixture(const char *path, const char *data)
{
	FILE *fp = fopen(path, "w");

	if (!fp)
		return -1;
	fputs(data, fp);
	fclose(fp);
	return 0;
}

static void test_sem_parse_count(void)
{
	struct gps_assist_data data;
	const char *path = "/tmp/test_sem.al3";

	TEST("SEM: parse 2 satellite entries");
	memset(&data, 0, sizeof(data));
	ASSERT_INT_EQ(write_fixture(path, sem_fixture), 0);
	ASSERT_INT_EQ(almanac_parse_sem(path, &data), 0);
	ASSERT_INT_EQ(data.num_alm, 2);
	unlink(path);
	PASS();
	return;
out:
	unlink(path);
}

static void test_sem_parse_prn(void)
{
	struct gps_assist_data data;
	const char *path = "/tmp/test_sem.al3";

	TEST("SEM: correct PRN values");
	memset(&data, 0, sizeof(data));
	write_fixture(path, sem_fixture);
	almanac_parse_sem(path, &data);
	ASSERT_INT_EQ(data.alm[0].prn, 1);
	ASSERT_INT_EQ(data.alm[1].prn, 15);
	unlink(path);
	PASS();
	return;
out:
	unlink(path);
}

static void test_sem_parse_header(void)
{
	struct gps_assist_data data;
	const char *path = "/tmp/test_sem.al3";

	TEST("SEM: week and toa from header");
	memset(&data, 0, sizeof(data));
	write_fixture(path, sem_fixture);
	almanac_parse_sem(path, &data);
	ASSERT_INT_EQ(data.alm[0].week, 2409);
	ASSERT_INT_EQ((int)data.alm[0].toa, 405504);
	ASSERT_INT_EQ(data.alm[1].week, 2409);
	ASSERT_INT_EQ((int)data.alm[1].toa, 405504);
	unlink(path);
	PASS();
	return;
out:
	unlink(path);
}

static void test_sem_parse_fields(void)
{
	struct gps_assist_data data;
	const char *path = "/tmp/test_sem.al3";

	TEST("SEM: orbital field values (PRN 1)");
	memset(&data, 0, sizeof(data));
	write_fixture(path, sem_fixture);
	almanac_parse_sem(path, &data);

	ASSERT_DBL_NEAR(data.alm[0].e, 5.0e-03, 1e-15);
	ASSERT_DBL_NEAR(data.alm[0].delta_i, 1.5e-02, 1e-15);
	ASSERT_DBL_NEAR(data.alm[0].omega_dot, -8.0e-09, 1e-20);
	ASSERT_DBL_NEAR(data.alm[0].sqrt_a, 5.1536e+03, 1e-10);
	ASSERT_DBL_NEAR(data.alm[0].omega0, 9.5e-01, 1e-15);
	ASSERT_DBL_NEAR(data.alm[0].omega, 5.0e-02, 1e-15);
	ASSERT_DBL_NEAR(data.alm[0].m0, -2.5e-01, 1e-15);
	ASSERT_DBL_NEAR(data.alm[0].af0, 1.0e-04, 1e-15);
	ASSERT_DBL_NEAR(data.alm[0].af1, -5.0e-12, 1e-25);
	ASSERT_INT_EQ(data.alm[0].health, 0);

	unlink(path);
	PASS();
	return;
out:
	unlink(path);
}

static void test_sem_parse_sv2(void)
{
	struct gps_assist_data data;
	const char *path = "/tmp/test_sem.al3";

	TEST("SEM: second satellite field values (PRN 15)");
	memset(&data, 0, sizeof(data));
	write_fixture(path, sem_fixture);
	almanac_parse_sem(path, &data);

	ASSERT_DBL_NEAR(data.alm[1].e, 1.0e-02, 1e-15);
	ASSERT_DBL_NEAR(data.alm[1].delta_i, 2.0e-02, 1e-15);
	ASSERT_DBL_NEAR(data.alm[1].sqrt_a, 5.1535e+03, 1e-10);
	ASSERT_DBL_NEAR(data.alm[1].omega0, -3.0e-01, 1e-15);
	ASSERT_DBL_NEAR(data.alm[1].af0, -2.0e-04, 1e-15);
	ASSERT_DBL_NEAR(data.alm[1].af1, 0.0, 1e-25);

	unlink(path);
	PASS();
	return;
out:
	unlink(path);
}

static void test_yuma_parse_count(void)
{
	struct gps_assist_data data;
	const char *path = "/tmp/test_yuma.txt";

	TEST("YUMA: parse 1 satellite entry");
	memset(&data, 0, sizeof(data));
	ASSERT_INT_EQ(write_fixture(path, yuma_fixture), 0);
	ASSERT_INT_EQ(almanac_parse_yuma(path, &data), 0);
	ASSERT_INT_EQ(data.num_alm, 1);
	unlink(path);
	PASS();
	return;
out:
	unlink(path);
}

static void test_yuma_parse_prn(void)
{
	struct gps_assist_data data;
	const char *path = "/tmp/test_yuma.txt";

	TEST("YUMA: correct PRN and week");
	memset(&data, 0, sizeof(data));
	write_fixture(path, yuma_fixture);
	almanac_parse_yuma(path, &data);
	ASSERT_INT_EQ(data.alm[0].prn, 1);
	ASSERT_INT_EQ(data.alm[0].week, 2409);
	ASSERT_INT_EQ((int)data.alm[0].toa, 405504);
	ASSERT_INT_EQ(data.alm[0].health, 0);
	unlink(path);
	PASS();
	return;
out:
	unlink(path);
}

static void test_yuma_rad_to_sc(void)
{
	struct gps_assist_data data;
	const char *path = "/tmp/test_yuma.txt";

	TEST("YUMA: radians -> semi-circles conversion");
	memset(&data, 0, sizeof(data));
	write_fixture(path, yuma_fixture);
	almanac_parse_yuma(path, &data);

	/* e is dimensionless, no conversion */
	ASSERT_DBL_NEAR(data.alm[0].e, 5.0e-03, 1e-15);

	/* sqrt_a: no conversion */
	ASSERT_DBL_NEAR(data.alm[0].sqrt_a, 5153.6, 1e-10);

	/* inclination=1.0 rad -> delta_i = (1/pi) - 0.3 sc */
	double expected_delta_i = (1.0 / M_PI) - 0.3;
	ASSERT_DBL_NEAR(data.alm[0].delta_i, expected_delta_i, 1e-12);

	/* omega_dot: -1e-8 rad/s -> -1e-8/pi sc/s */
	double expected_omega_dot = -1.0e-08 / M_PI;
	ASSERT_DBL_NEAR(data.alm[0].omega_dot, expected_omega_dot, 1e-20);

	/* omega0: 2.0 rad -> 2.0/pi sc */
	double expected_omega0 = 2.0 / M_PI;
	ASSERT_DBL_NEAR(data.alm[0].omega0, expected_omega0, 1e-12);

	/* omega: 0.5 rad -> 0.5/pi sc */
	double expected_omega = 0.5 / M_PI;
	ASSERT_DBL_NEAR(data.alm[0].omega, expected_omega, 1e-12);

	/* m0: -1.0 rad -> -1.0/pi sc */
	double expected_m0 = -1.0 / M_PI;
	ASSERT_DBL_NEAR(data.alm[0].m0, expected_m0, 1e-12);

	/* af0, af1: seconds, no conversion */
	ASSERT_DBL_NEAR(data.alm[0].af0, 1.0e-04, 1e-15);
	ASSERT_DBL_NEAR(data.alm[0].af1, -5.0e-12, 1e-25);

	unlink(path);
	PASS();
	return;
out:
	unlink(path);
}

static void test_yuma_sem_agreement(void)
{
	struct gps_assist_data sem_data, yuma_data;
	const char *sem_path = "/tmp/test_sem.al3";
	const char *yuma_path = "/tmp/test_yuma.txt";

	TEST("YUMA/SEM: same satellite gives matching values");
	memset(&sem_data, 0, sizeof(sem_data));
	memset(&yuma_data, 0, sizeof(yuma_data));
	write_fixture(sem_path, sem_fixture);
	write_fixture(yuma_path, yuma_fixture);
	almanac_parse_sem(sem_path, &sem_data);
	almanac_parse_yuma(yuma_path, &yuma_data);

	/* Both PRN 1, same e and sqrt_a (no conversion) */
	ASSERT_DBL_NEAR(sem_data.alm[0].e, yuma_data.alm[0].e, 1e-15);
	ASSERT_DBL_NEAR(sem_data.alm[0].sqrt_a, yuma_data.alm[0].sqrt_a, 1e-10);
	ASSERT_DBL_NEAR(sem_data.alm[0].af0, yuma_data.alm[0].af0, 1e-15);
	ASSERT_DBL_NEAR(sem_data.alm[0].af1, yuma_data.alm[0].af1, 1e-25);

	unlink(sem_path);
	unlink(yuma_path);
	PASS();
	return;
out:
	unlink(sem_path);
	unlink(yuma_path);
}

int main(void)
{
	fprintf(stderr, "=== Almanac parser tests ===\n");

	test_sem_parse_count();
	test_sem_parse_prn();
	test_sem_parse_header();
	test_sem_parse_fields();
	test_sem_parse_sv2();
	test_yuma_parse_count();
	test_yuma_parse_prn();
	test_yuma_rad_to_sc();
	test_yuma_sem_agreement();

	fprintf(stderr, "\n%d/%d tests passed\n", tests_passed, tests_run);
	return tests_passed == tests_run ? 0 : 1;
}
