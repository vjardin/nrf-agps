/*
 * test_sqlitedb.c - SQLite storage tests
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sqlite3.h>

#include "gps_assist.h"
#include "sqlitedb.h"

static int tests_run, tests_passed;

#define ASSERT(cond, msg) do { \
	tests_run++; \
	if (!(cond)) { \
		fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__); \
	} else { \
		tests_passed++; \
	} \
} while (0)

#define ASSERT_DBL(a, b, msg) \
	ASSERT(fabs((a) - (b)) < 1e-15, msg)

static struct gps_assist_data test_data;

static void init_test_data(void)
{
	memset(&test_data, 0, sizeof(test_data));

	test_data.timestamp = 1773064800U;
	test_data.gps_week = 2409;
	test_data.num_sv = 2;

	/* Ionosphere */
	test_data.iono.alpha[0] = 1.118e-08;
	test_data.iono.alpha[1] = 0.0;
	test_data.iono.alpha[2] = -5.961e-08;
	test_data.iono.alpha[3] = 0.0;
	test_data.iono.beta[0] = 9.011e+04;
	test_data.iono.beta[1] = 0.0;
	test_data.iono.beta[2] = -1.966e+05;
	test_data.iono.beta[3] = 0.0;

	/* UTC */
	test_data.utc.a0 = 1.863e-09;
	test_data.utc.a1 = 4.441e-15;
	test_data.utc.tot = 233472;
	test_data.utc.wnt = 2409;
	test_data.utc.dt_ls = 18;

	/* Location */
	test_data.location.latitude = 48.8530;
	test_data.location.longitude = 2.3498;
	test_data.location.altitude = 35;
	test_data.location.valid = 1;

	/* GPS ephemeris */
	test_data.sv[0].prn = 1;
	test_data.sv[0].health = 0;
	test_data.sv[0].iodc = 933;
	test_data.sv[0].iode = 165;
	test_data.sv[0].week = 2409;
	test_data.sv[0].toe = 396000;
	test_data.sv[0].toc = 396000;
	test_data.sv[0].af0 = -2.578310668468e-05;
	test_data.sv[0].af1 = -2.842170943040e-14;
	test_data.sv[0].af2 = 0.0;
	test_data.sv[0].sqrt_a = 5153.648925781250;
	test_data.sv[0].e = 5.483717424795e-03;
	test_data.sv[0].i0 = 9.755508981940e-01;
	test_data.sv[0].omega0 = -2.025233279608;
	test_data.sv[0].omega = -1.722784098283;
	test_data.sv[0].m0 = 1.296410068666;
	test_data.sv[0].delta_n = 4.200289988741e-09;
	test_data.sv[0].omega_dot = -7.793218395069e-09;
	test_data.sv[0].idot = -2.078767516450e-10;
	test_data.sv[0].cuc = -2.935528755188e-06;
	test_data.sv[0].cus = 8.162856101990e-06;
	test_data.sv[0].crc = 222.65625;
	test_data.sv[0].crs = -48.0625;
	test_data.sv[0].cic = 1.043081283569e-07;
	test_data.sv[0].cis = 1.303851604462e-07;
	test_data.sv[0].tgd = -1.117587089539e-08;

	test_data.sv[1].prn = 3;
	test_data.sv[1].health = 0;
	test_data.sv[1].iodc = 42;
	test_data.sv[1].iode = 42;
	test_data.sv[1].week = 2409;
	test_data.sv[1].toe = 388800;
	test_data.sv[1].toc = 388800;
	test_data.sv[1].af0 = 3.510899841785e-04;
	test_data.sv[1].af1 = 0.0;
	test_data.sv[1].af2 = 0.0;
	test_data.sv[1].sqrt_a = 5153.600097656250;
	test_data.sv[1].e = 1.209285645746e-02;
	test_data.sv[1].i0 = 9.535298667798e-01;
	test_data.sv[1].omega0 = 2.477804929697;
	test_data.sv[1].omega = 0.580483831447;
	test_data.sv[1].m0 = -1.846729028753;
	test_data.sv[1].delta_n = 4.664512974453e-09;
	test_data.sv[1].omega_dot = -8.179282714095e-09;
	test_data.sv[1].idot = 2.285770803893e-10;
	test_data.sv[1].cuc = -3.576278686523e-06;
	test_data.sv[1].cus = 5.448237061501e-06;
	test_data.sv[1].crc = 270.15625;
	test_data.sv[1].crs = -30.84375;
	test_data.sv[1].cic = -5.587935447693e-08;
	test_data.sv[1].cis = 6.332993507385e-08;
	test_data.sv[1].tgd = -4.656612873077e-09;

	/* QZSS */
	test_data.num_qzss = 1;
	test_data.qzss[0].prn = 193;
	test_data.qzss[0].health = 0;
	test_data.qzss[0].iodc = 10;
	test_data.qzss[0].iode = 10;
	test_data.qzss[0].week = 2409;
	test_data.qzss[0].toe = 396000;
	test_data.qzss[0].toc = 396000;
	test_data.qzss[0].af0 = -1.0e-06;
	test_data.qzss[0].af1 = 0.0;
	test_data.qzss[0].af2 = 0.0;
	test_data.qzss[0].sqrt_a = 6493.296875;
	test_data.qzss[0].e = 0.075;
	test_data.qzss[0].i0 = 0.7330382858;
	test_data.qzss[0].omega0 = 2.356194490;
	test_data.qzss[0].omega = -1.570796327;
	test_data.qzss[0].m0 = 0.785398163;
	test_data.qzss[0].delta_n = 3.0e-09;
	test_data.qzss[0].omega_dot = -6.0e-09;
	test_data.qzss[0].idot = 1.0e-10;
	test_data.qzss[0].cuc = 0.0;
	test_data.qzss[0].cus = 0.0;
	test_data.qzss[0].crc = 0.0;
	test_data.qzss[0].crs = 0.0;
	test_data.qzss[0].cic = 0.0;
	test_data.qzss[0].cis = 0.0;
	test_data.qzss[0].tgd = 0.0;

	/* Almanac */
	test_data.num_alm = 1;
	test_data.alm[0].prn = 1;
	test_data.alm[0].health = 0;
	test_data.alm[0].ioda = 2;
	test_data.alm[0].week = 2409;
	test_data.alm[0].toa = 405504;
	test_data.alm[0].e = 5.48e-03;
	test_data.alm[0].delta_i = -1.54e-03;
	test_data.alm[0].omega_dot = -2.48e-09;
	test_data.alm[0].sqrt_a = 5153.65;
	test_data.alm[0].omega0 = -0.644;
	test_data.alm[0].omega = -0.548;
	test_data.alm[0].m0 = 0.412;
	test_data.alm[0].af0 = -2.58e-05;
	test_data.alm[0].af1 = -2.84e-14;
}

static int query_int(sqlite3 *db, const char *sql)
{
	sqlite3_stmt *stmt;
	int val = -1;

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
		if (sqlite3_step(stmt) == SQLITE_ROW)
			val = sqlite3_column_int(stmt, 0);
		sqlite3_finalize(stmt);
	}
	return val;
}

static double query_double(sqlite3 *db, const char *sql)
{
	sqlite3_stmt *stmt;
	double val = 0.0;

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
		if (sqlite3_step(stmt) == SQLITE_ROW)
			val = sqlite3_column_double(stmt, 0);
		sqlite3_finalize(stmt);
	}
	return val;
}

static const char *query_text(sqlite3 *db, const char *sql,
			      char *buf, size_t len)
{
	sqlite3_stmt *stmt;

	buf[0] = '\0';
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *t = (const char *)sqlite3_column_text(stmt, 0);
			if (t)
				snprintf(buf, len, "%s", t);
		}
		sqlite3_finalize(stmt);
	}
	return buf;
}

static void test_store_and_read(void)
{
	char db_path[] = "/tmp/test_sqlitedb_XXXXXX";
	int fd = mkstemp(db_path);
	sqlite3 *db;
	char buf[256];

	printf("=== test_sqlitedb: store and read ===\n");

	ASSERT(fd >= 0, "mkstemp");
	close(fd);

	/* Store data */
	int rc = sqlitedb_store(db_path, &test_data, "test_fixture.rnx");
	ASSERT(rc == 0, "sqlitedb_store returns 0");

	/* Open and verify */
	ASSERT(sqlite3_open(db_path, &db) == SQLITE_OK, "sqlite3_open");

	/* Metadata */
	ASSERT(query_int(db, "SELECT COUNT(*) FROM metadata") == 1,
	       "metadata: 1 row");
	ASSERT(query_int(db, "SELECT timestamp FROM metadata WHERE id=1")
	       == (int)test_data.timestamp, "metadata: timestamp");
	ASSERT(query_int(db, "SELECT gps_week FROM metadata WHERE id=1")
	       == test_data.gps_week, "metadata: gps_week");
	query_text(db, "SELECT source_name FROM metadata WHERE id=1",
		   buf, sizeof(buf));
	ASSERT(strcmp(buf, "test_fixture.rnx") == 0, "metadata: source_name");
	ASSERT_DBL(query_double(db,
		"SELECT ref_latitude FROM metadata WHERE id=1"),
		48.8530, "metadata: ref_latitude");
	ASSERT_DBL(query_double(db,
		"SELECT ref_longitude FROM metadata WHERE id=1"),
		2.3498, "metadata: ref_longitude");

	/* GPS ephemeris count */
	ASSERT(query_int(db,
		"SELECT COUNT(*) FROM ephemeris WHERE constellation='GPS'")
		== 2, "ephemeris: 2 GPS rows");

	/* GPS ephemeris PRN 1 fields */
	ASSERT(query_int(db,
		"SELECT prn FROM ephemeris WHERE constellation='GPS'"
		" AND prn=1") == 1, "ephemeris: PRN 1 exists");
	ASSERT(query_int(db,
		"SELECT iodc FROM ephemeris WHERE constellation='GPS'"
		" AND prn=1") == 933, "ephemeris: PRN 1 iodc");
	ASSERT(query_int(db,
		"SELECT iode FROM ephemeris WHERE constellation='GPS'"
		" AND prn=1") == 165, "ephemeris: PRN 1 iode");
	ASSERT(query_int(db,
		"SELECT toe FROM ephemeris WHERE constellation='GPS'"
		" AND prn=1") == 396000, "ephemeris: PRN 1 toe");
	ASSERT_DBL(query_double(db,
		"SELECT af0 FROM ephemeris WHERE constellation='GPS'"
		" AND prn=1"),
		-2.578310668468e-05, "ephemeris: PRN 1 af0");
	ASSERT_DBL(query_double(db,
		"SELECT sqrt_a FROM ephemeris WHERE constellation='GPS'"
		" AND prn=1"),
		5153.648925781250, "ephemeris: PRN 1 sqrt_a");
	ASSERT_DBL(query_double(db,
		"SELECT e FROM ephemeris WHERE constellation='GPS'"
		" AND prn=1"),
		5.483717424795e-03, "ephemeris: PRN 1 eccentricity");
	ASSERT_DBL(query_double(db,
		"SELECT m0 FROM ephemeris WHERE constellation='GPS'"
		" AND prn=1"),
		1.296410068666, "ephemeris: PRN 1 m0");
	ASSERT_DBL(query_double(db,
		"SELECT tgd FROM ephemeris WHERE constellation='GPS'"
		" AND prn=1"),
		-1.117587089539e-08, "ephemeris: PRN 1 tgd");

	/* GPS ephemeris PRN 3 */
	ASSERT(query_int(db,
		"SELECT prn FROM ephemeris WHERE constellation='GPS'"
		" AND prn=3") == 3, "ephemeris: PRN 3 exists");
	ASSERT_DBL(query_double(db,
		"SELECT af0 FROM ephemeris WHERE constellation='GPS'"
		" AND prn=3"),
		3.510899841785e-04, "ephemeris: PRN 3 af0");

	/* QZSS ephemeris */
	ASSERT(query_int(db,
		"SELECT COUNT(*) FROM ephemeris WHERE constellation='QZSS'")
		== 1, "ephemeris: 1 QZSS row");
	ASSERT(query_int(db,
		"SELECT prn FROM ephemeris WHERE constellation='QZSS'")
		== 193, "ephemeris: QZSS PRN 193");
	ASSERT_DBL(query_double(db,
		"SELECT sqrt_a FROM ephemeris WHERE constellation='QZSS'"
		" AND prn=193"),
		6493.296875, "ephemeris: QZSS sqrt_a");

	/* Almanac */
	ASSERT(query_int(db, "SELECT COUNT(*) FROM almanac") == 1,
	       "almanac: 1 row");
	ASSERT(query_int(db, "SELECT prn FROM almanac WHERE metadata_id=1")
	       == 1, "almanac: PRN 1");
	ASSERT(query_int(db, "SELECT ioda FROM almanac WHERE prn=1")
	       == 2, "almanac: ioda");
	ASSERT_DBL(query_double(db, "SELECT e FROM almanac WHERE prn=1"),
		5.48e-03, "almanac: eccentricity");
	ASSERT_DBL(query_double(db, "SELECT af0 FROM almanac WHERE prn=1"),
		-2.58e-05, "almanac: af0");

	/* Ionosphere */
	ASSERT(query_int(db, "SELECT COUNT(*) FROM ionosphere") == 1,
	       "ionosphere: 1 row");
	ASSERT_DBL(query_double(db,
		"SELECT alpha0 FROM ionosphere WHERE metadata_id=1"),
		1.118e-08, "ionosphere: alpha0");
	ASSERT_DBL(query_double(db,
		"SELECT beta2 FROM ionosphere WHERE metadata_id=1"),
		-1.966e+05, "ionosphere: beta2");

	/* UTC params */
	ASSERT(query_int(db, "SELECT COUNT(*) FROM utc_params") == 1,
	       "utc_params: 1 row");
	ASSERT_DBL(query_double(db,
		"SELECT a0 FROM utc_params WHERE metadata_id=1"),
		1.863e-09, "utc_params: a0");
	ASSERT(query_int(db,
		"SELECT dt_ls FROM utc_params WHERE metadata_id=1")
		== 18, "utc_params: dt_ls");
	ASSERT(query_int(db,
		"SELECT tot FROM utc_params WHERE metadata_id=1")
		== 233472, "utc_params: tot");

	sqlite3_close(db);
	unlink(db_path);

	printf("  %d/%d tests passed\n", tests_passed, tests_run);
}

static void test_multiple_datasets(void)
{
	char db_path[] = "/tmp/test_sqlitedb_multi_XXXXXX";
	int fd = mkstemp(db_path);
	sqlite3 *db;
	int base = tests_run;
	int base_pass = tests_passed;

	printf("=== test_sqlitedb: multiple datasets ===\n");

	ASSERT(fd >= 0, "mkstemp");
	close(fd);

	/* Store twice */
	ASSERT(sqlitedb_store(db_path, &test_data, "first.rnx") == 0,
	       "first store");
	ASSERT(sqlitedb_store(db_path, &test_data, "second.rnx") == 0,
	       "second store");

	ASSERT(sqlite3_open(db_path, &db) == SQLITE_OK, "sqlite3_open");

	ASSERT(query_int(db, "SELECT COUNT(*) FROM metadata") == 2,
	       "metadata: 2 rows");
	ASSERT(query_int(db, "SELECT MAX(id) FROM metadata") == 2,
	       "metadata: max id is 2");

	/* Each dataset has its own ephemeris rows */
	ASSERT(query_int(db,
		"SELECT COUNT(*) FROM ephemeris WHERE metadata_id=1") == 3,
	       "dataset 1: 3 ephemeris (2 GPS + 1 QZSS)");
	ASSERT(query_int(db,
		"SELECT COUNT(*) FROM ephemeris WHERE metadata_id=2") == 3,
	       "dataset 2: 3 ephemeris (2 GPS + 1 QZSS)");

	sqlite3_close(db);
	unlink(db_path);

	printf("  %d/%d tests passed\n",
	       tests_passed - base_pass, tests_run - base);
}

static void test_no_almanac(void)
{
	char db_path[] = "/tmp/test_sqlitedb_noalm_XXXXXX";
	int fd = mkstemp(db_path);
	sqlite3 *db;
	struct gps_assist_data data;
	int base = tests_run;
	int base_pass = tests_passed;

	printf("=== test_sqlitedb: no almanac ===\n");

	ASSERT(fd >= 0, "mkstemp");
	close(fd);

	memcpy(&data, &test_data, sizeof(data));
	data.num_alm = 0;
	data.num_qzss = 0;

	ASSERT(sqlitedb_store(db_path, &data, "noalm.rnx") == 0, "store");

	ASSERT(sqlite3_open(db_path, &db) == SQLITE_OK, "sqlite3_open");
	ASSERT(query_int(db, "SELECT COUNT(*) FROM almanac") == 0,
	       "almanac: 0 rows");
	ASSERT(query_int(db,
		"SELECT COUNT(*) FROM ephemeris WHERE constellation='QZSS'")
		== 0, "ephemeris: 0 QZSS rows");
	ASSERT(query_int(db,
		"SELECT COUNT(*) FROM ephemeris WHERE constellation='GPS'")
		== 2, "ephemeris: 2 GPS rows");

	sqlite3_close(db);
	unlink(db_path);

	printf("  %d/%d tests passed\n",
	       tests_passed - base_pass, tests_run - base);
}

int main(void)
{
	init_test_data();

	test_store_and_read();
	test_multiple_datasets();
	test_no_almanac();

	printf("\ntest_sqlitedb: %d/%d passed\n", tests_passed, tests_run);
	return tests_passed == tests_run ? 0 : 1;
}
