/*
 * sqlitedb.c - SQLite storage for GPS assistance data
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>

#include "sqlitedb.h"

static const char schema_sql[] =
	"CREATE TABLE IF NOT EXISTS metadata ("
	"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
	"  timestamp INTEGER NOT NULL,"
	"  gps_week INTEGER NOT NULL,"
	"  source_name TEXT,"
	"  ref_latitude REAL,"
	"  ref_longitude REAL,"
	"  ref_altitude INTEGER,"
	"  created_at TEXT DEFAULT (datetime('now'))"
	");"
	"CREATE TABLE IF NOT EXISTS ephemeris ("
	"  metadata_id INTEGER NOT NULL REFERENCES metadata(id),"
	"  constellation TEXT NOT NULL DEFAULT 'GPS',"
	"  prn INTEGER NOT NULL,"
	"  health INTEGER NOT NULL,"
	"  iodc INTEGER NOT NULL,"
	"  iode INTEGER NOT NULL,"
	"  week INTEGER NOT NULL,"
	"  toe INTEGER NOT NULL,"
	"  toc INTEGER NOT NULL,"
	"  af0 REAL NOT NULL,"
	"  af1 REAL NOT NULL,"
	"  af2 REAL NOT NULL,"
	"  sqrt_a REAL NOT NULL,"
	"  e REAL NOT NULL,"
	"  i0 REAL NOT NULL,"
	"  omega0 REAL NOT NULL,"
	"  omega REAL NOT NULL,"
	"  m0 REAL NOT NULL,"
	"  delta_n REAL NOT NULL,"
	"  omega_dot REAL NOT NULL,"
	"  idot REAL NOT NULL,"
	"  cuc REAL NOT NULL,"
	"  cus REAL NOT NULL,"
	"  crc REAL NOT NULL,"
	"  crs REAL NOT NULL,"
	"  cic REAL NOT NULL,"
	"  cis REAL NOT NULL,"
	"  tgd REAL NOT NULL,"
	"  PRIMARY KEY (metadata_id, constellation, prn)"
	");"
	"CREATE TABLE IF NOT EXISTS almanac ("
	"  metadata_id INTEGER NOT NULL REFERENCES metadata(id),"
	"  prn INTEGER NOT NULL,"
	"  health INTEGER NOT NULL,"
	"  ioda INTEGER NOT NULL,"
	"  week INTEGER NOT NULL,"
	"  toa INTEGER NOT NULL,"
	"  e REAL NOT NULL,"
	"  delta_i REAL NOT NULL,"
	"  omega_dot REAL NOT NULL,"
	"  sqrt_a REAL NOT NULL,"
	"  omega0 REAL NOT NULL,"
	"  omega REAL NOT NULL,"
	"  m0 REAL NOT NULL,"
	"  af0 REAL NOT NULL,"
	"  af1 REAL NOT NULL,"
	"  PRIMARY KEY (metadata_id, prn)"
	");"
	"CREATE TABLE IF NOT EXISTS ionosphere ("
	"  metadata_id INTEGER PRIMARY KEY REFERENCES metadata(id),"
	"  alpha0 REAL NOT NULL,"
	"  alpha1 REAL NOT NULL,"
	"  alpha2 REAL NOT NULL,"
	"  alpha3 REAL NOT NULL,"
	"  beta0 REAL NOT NULL,"
	"  beta1 REAL NOT NULL,"
	"  beta2 REAL NOT NULL,"
	"  beta3 REAL NOT NULL"
	");"
	"CREATE TABLE IF NOT EXISTS utc_params ("
	"  metadata_id INTEGER PRIMARY KEY REFERENCES metadata(id),"
	"  a0 REAL NOT NULL,"
	"  a1 REAL NOT NULL,"
	"  tot INTEGER NOT NULL,"
	"  wnt INTEGER NOT NULL,"
	"  dt_ls INTEGER NOT NULL"
	");";

static int create_schema(sqlite3 *db)
{
	char *err = NULL;

	if (sqlite3_exec(db, schema_sql, NULL, NULL, &err) != SQLITE_OK) {
		fprintf(stderr, "SQLite schema error: %s\n", err);
		sqlite3_free(err);
		return -1;
	}
	return 0;
}

static int insert_metadata(sqlite3 *db, const struct gps_assist_data *data,
			   const char *source_name, sqlite3_int64 *out_id)
{
	sqlite3_stmt *stmt;
	const char *sql =
		"INSERT INTO metadata (timestamp, gps_week, source_name,"
		" ref_latitude, ref_longitude, ref_altitude)"
		" VALUES (?, ?, ?, ?, ?, ?)";
	int rc;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	sqlite3_bind_int(stmt, 1, data->timestamp);
	sqlite3_bind_int(stmt, 2, data->gps_week);
	if (source_name)
		sqlite3_bind_text(stmt, 3, source_name, -1, SQLITE_STATIC);
	else
		sqlite3_bind_null(stmt, 3);
	if (data->location.valid) {
		sqlite3_bind_double(stmt, 4, data->location.latitude);
		sqlite3_bind_double(stmt, 5, data->location.longitude);
		sqlite3_bind_int(stmt, 6, data->location.altitude);
	} else {
		sqlite3_bind_null(stmt, 4);
		sqlite3_bind_null(stmt, 5);
		sqlite3_bind_null(stmt, 6);
	}

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE)
		return -1;

	*out_id = sqlite3_last_insert_rowid(db);
	return 0;
}

static int insert_ephemeris(sqlite3 *db, sqlite3_int64 meta_id,
			    const struct gps_ephemeris *sv, int count,
			    const char *constellation)
{
	sqlite3_stmt *stmt;
	const char *sql =
		"INSERT INTO ephemeris (metadata_id, constellation, prn,"
		" health, iodc, iode, week, toe, toc,"
		" af0, af1, af2, sqrt_a, e, i0, omega0, omega, m0,"
		" delta_n, omega_dot, idot,"
		" cuc, cus, crc, crs, cic, cis, tgd)"
		" VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
	int rc;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	for (int i = 0; i < count; i++) {
		const struct gps_ephemeris *s = &sv[i];

		sqlite3_reset(stmt);
		sqlite3_bind_int64(stmt, 1, meta_id);
		sqlite3_bind_text(stmt, 2, constellation, -1, SQLITE_STATIC);
		sqlite3_bind_int(stmt, 3, s->prn);
		sqlite3_bind_int(stmt, 4, s->health);
		sqlite3_bind_int(stmt, 5, s->iodc);
		sqlite3_bind_int(stmt, 6, s->iode);
		sqlite3_bind_int(stmt, 7, s->week);
		sqlite3_bind_int(stmt, 8, s->toe);
		sqlite3_bind_int(stmt, 9, s->toc);
		sqlite3_bind_double(stmt, 10, s->af0);
		sqlite3_bind_double(stmt, 11, s->af1);
		sqlite3_bind_double(stmt, 12, s->af2);
		sqlite3_bind_double(stmt, 13, s->sqrt_a);
		sqlite3_bind_double(stmt, 14, s->e);
		sqlite3_bind_double(stmt, 15, s->i0);
		sqlite3_bind_double(stmt, 16, s->omega0);
		sqlite3_bind_double(stmt, 17, s->omega);
		sqlite3_bind_double(stmt, 18, s->m0);
		sqlite3_bind_double(stmt, 19, s->delta_n);
		sqlite3_bind_double(stmt, 20, s->omega_dot);
		sqlite3_bind_double(stmt, 21, s->idot);
		sqlite3_bind_double(stmt, 22, s->cuc);
		sqlite3_bind_double(stmt, 23, s->cus);
		sqlite3_bind_double(stmt, 24, s->crc);
		sqlite3_bind_double(stmt, 25, s->crs);
		sqlite3_bind_double(stmt, 26, s->cic);
		sqlite3_bind_double(stmt, 27, s->cis);
		sqlite3_bind_double(stmt, 28, s->tgd);

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(stmt);
			return -1;
		}
	}

	sqlite3_finalize(stmt);
	return 0;
}

static int insert_almanac(sqlite3 *db, sqlite3_int64 meta_id,
			  const struct gps_almanac *alm, int count)
{
	sqlite3_stmt *stmt;
	const char *sql =
		"INSERT INTO almanac (metadata_id, prn, health, ioda,"
		" week, toa, e, delta_i, omega_dot, sqrt_a,"
		" omega0, omega, m0, af0, af1)"
		" VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
	int rc;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	for (int i = 0; i < count; i++) {
		const struct gps_almanac *a = &alm[i];

		sqlite3_reset(stmt);
		sqlite3_bind_int64(stmt, 1, meta_id);
		sqlite3_bind_int(stmt, 2, a->prn);
		sqlite3_bind_int(stmt, 3, a->health);
		sqlite3_bind_int(stmt, 4, a->ioda);
		sqlite3_bind_int(stmt, 5, a->week);
		sqlite3_bind_int(stmt, 6, a->toa);
		sqlite3_bind_double(stmt, 7, a->e);
		sqlite3_bind_double(stmt, 8, a->delta_i);
		sqlite3_bind_double(stmt, 9, a->omega_dot);
		sqlite3_bind_double(stmt, 10, a->sqrt_a);
		sqlite3_bind_double(stmt, 11, a->omega0);
		sqlite3_bind_double(stmt, 12, a->omega);
		sqlite3_bind_double(stmt, 13, a->m0);
		sqlite3_bind_double(stmt, 14, a->af0);
		sqlite3_bind_double(stmt, 15, a->af1);

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(stmt);
			return -1;
		}
	}

	sqlite3_finalize(stmt);
	return 0;
}

static int insert_ionosphere(sqlite3 *db, sqlite3_int64 meta_id,
			     const struct gps_iono *iono)
{
	sqlite3_stmt *stmt;
	const char *sql =
		"INSERT INTO ionosphere (metadata_id,"
		" alpha0, alpha1, alpha2, alpha3,"
		" beta0, beta1, beta2, beta3)"
		" VALUES (?,?,?,?,?,?,?,?,?)";
	int rc;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	sqlite3_bind_int64(stmt, 1, meta_id);
	sqlite3_bind_double(stmt, 2, iono->alpha[0]);
	sqlite3_bind_double(stmt, 3, iono->alpha[1]);
	sqlite3_bind_double(stmt, 4, iono->alpha[2]);
	sqlite3_bind_double(stmt, 5, iono->alpha[3]);
	sqlite3_bind_double(stmt, 6, iono->beta[0]);
	sqlite3_bind_double(stmt, 7, iono->beta[1]);
	sqlite3_bind_double(stmt, 8, iono->beta[2]);
	sqlite3_bind_double(stmt, 9, iono->beta[3]);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	return (rc == SQLITE_DONE) ? 0 : -1;
}

static int insert_utc(sqlite3 *db, sqlite3_int64 meta_id,
		      const struct gps_utc *utc)
{
	sqlite3_stmt *stmt;
	const char *sql =
		"INSERT INTO utc_params (metadata_id, a0, a1, tot, wnt, dt_ls)"
		" VALUES (?,?,?,?,?,?)";
	int rc;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	sqlite3_bind_int64(stmt, 1, meta_id);
	sqlite3_bind_double(stmt, 2, utc->a0);
	sqlite3_bind_double(stmt, 3, utc->a1);
	sqlite3_bind_int(stmt, 4, utc->tot);
	sqlite3_bind_int(stmt, 5, utc->wnt);
	sqlite3_bind_int(stmt, 6, utc->dt_ls);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	return (rc == SQLITE_DONE) ? 0 : -1;
}

static int read_metadata(sqlite3 *db, struct gps_assist_data *data,
			 sqlite3_int64 *out_id)
{
	sqlite3_stmt *stmt;
	const char *sql =
		"SELECT id, timestamp, gps_week,"
		" ref_latitude, ref_longitude, ref_altitude"
		" FROM metadata ORDER BY id DESC LIMIT 1";
	int rc;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return -1;
	}

	*out_id = sqlite3_column_int64(stmt, 0);
	data->timestamp = (uint32_t)sqlite3_column_int(stmt, 1);
	data->gps_week = (uint16_t)sqlite3_column_int(stmt, 2);

	if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
		data->location.latitude = sqlite3_column_double(stmt, 3);
		data->location.longitude = sqlite3_column_double(stmt, 4);
		data->location.altitude = (int16_t)sqlite3_column_int(stmt, 5);
		data->location.valid = 1;
	}

	sqlite3_finalize(stmt);
	return 0;
}

static int read_ephemeris(sqlite3 *db, sqlite3_int64 meta_id,
			  struct gps_assist_data *data)
{
	sqlite3_stmt *stmt;
	const char *sql =
		"SELECT constellation, prn, health, iodc, iode, week,"
		" toe, toc, af0, af1, af2, sqrt_a, e, i0, omega0, omega,"
		" m0, delta_n, omega_dot, idot,"
		" cuc, cus, crc, crs, cic, cis, tgd"
		" FROM ephemeris WHERE metadata_id = ?";
	int rc;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	sqlite3_bind_int64(stmt, 1, meta_id);

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		const char *cons = (const char *)sqlite3_column_text(stmt, 0);
		struct gps_ephemeris *s;

		if (cons && cons[0] == 'Q') {
			if (data->num_qzss >= QZSS_MAX_SATS)
				continue;
			s = &data->qzss[data->num_qzss++];
		} else {
			if (data->num_sv >= GPS_MAX_SATS)
				continue;
			s = &data->sv[data->num_sv++];
		}

		s->prn       = (uint8_t)sqlite3_column_int(stmt, 1);
		s->health    = (uint8_t)sqlite3_column_int(stmt, 2);
		s->iodc      = (uint16_t)sqlite3_column_int(stmt, 3);
		s->iode      = (uint8_t)sqlite3_column_int(stmt, 4);
		s->week      = (uint16_t)sqlite3_column_int(stmt, 5);
		s->toe       = (uint32_t)sqlite3_column_int(stmt, 6);
		s->toc       = (uint32_t)sqlite3_column_int(stmt, 7);
		s->af0       = sqlite3_column_double(stmt, 8);
		s->af1       = sqlite3_column_double(stmt, 9);
		s->af2       = sqlite3_column_double(stmt, 10);
		s->sqrt_a    = sqlite3_column_double(stmt, 11);
		s->e         = sqlite3_column_double(stmt, 12);
		s->i0        = sqlite3_column_double(stmt, 13);
		s->omega0    = sqlite3_column_double(stmt, 14);
		s->omega     = sqlite3_column_double(stmt, 15);
		s->m0        = sqlite3_column_double(stmt, 16);
		s->delta_n   = sqlite3_column_double(stmt, 17);
		s->omega_dot = sqlite3_column_double(stmt, 18);
		s->idot      = sqlite3_column_double(stmt, 19);
		s->cuc       = sqlite3_column_double(stmt, 20);
		s->cus       = sqlite3_column_double(stmt, 21);
		s->crc       = sqlite3_column_double(stmt, 22);
		s->crs       = sqlite3_column_double(stmt, 23);
		s->cic       = sqlite3_column_double(stmt, 24);
		s->cis       = sqlite3_column_double(stmt, 25);
		s->tgd       = sqlite3_column_double(stmt, 26);
	}

	sqlite3_finalize(stmt);
	return 0;
}

static int read_almanac(sqlite3 *db, sqlite3_int64 meta_id,
			struct gps_assist_data *data)
{
	sqlite3_stmt *stmt;
	const char *sql =
		"SELECT prn, health, ioda, week, toa,"
		" e, delta_i, omega_dot, sqrt_a,"
		" omega0, omega, m0, af0, af1"
		" FROM almanac WHERE metadata_id = ?";
	int rc;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	sqlite3_bind_int64(stmt, 1, meta_id);

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (data->num_alm >= GPS_MAX_SATS)
			continue;
		struct gps_almanac *a = &data->alm[data->num_alm++];

		a->prn       = (uint8_t)sqlite3_column_int(stmt, 0);
		a->health    = (uint8_t)sqlite3_column_int(stmt, 1);
		a->ioda      = (uint8_t)sqlite3_column_int(stmt, 2);
		a->week      = (uint16_t)sqlite3_column_int(stmt, 3);
		a->toa       = (uint32_t)sqlite3_column_int(stmt, 4);
		a->e         = sqlite3_column_double(stmt, 5);
		a->delta_i   = sqlite3_column_double(stmt, 6);
		a->omega_dot = sqlite3_column_double(stmt, 7);
		a->sqrt_a    = sqlite3_column_double(stmt, 8);
		a->omega0    = sqlite3_column_double(stmt, 9);
		a->omega     = sqlite3_column_double(stmt, 10);
		a->m0        = sqlite3_column_double(stmt, 11);
		a->af0       = sqlite3_column_double(stmt, 12);
		a->af1       = sqlite3_column_double(stmt, 13);
	}

	sqlite3_finalize(stmt);
	return 0;
}

static int read_ionosphere(sqlite3 *db, sqlite3_int64 meta_id,
			   struct gps_iono *iono)
{
	sqlite3_stmt *stmt;
	const char *sql =
		"SELECT alpha0, alpha1, alpha2, alpha3,"
		" beta0, beta1, beta2, beta3"
		" FROM ionosphere WHERE metadata_id = ?";
	int rc;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	sqlite3_bind_int64(stmt, 1, meta_id);

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		iono->alpha[0] = sqlite3_column_double(stmt, 0);
		iono->alpha[1] = sqlite3_column_double(stmt, 1);
		iono->alpha[2] = sqlite3_column_double(stmt, 2);
		iono->alpha[3] = sqlite3_column_double(stmt, 3);
		iono->beta[0]  = sqlite3_column_double(stmt, 4);
		iono->beta[1]  = sqlite3_column_double(stmt, 5);
		iono->beta[2]  = sqlite3_column_double(stmt, 6);
		iono->beta[3]  = sqlite3_column_double(stmt, 7);
	}

	sqlite3_finalize(stmt);
	return 0;
}

static int read_utc(sqlite3 *db, sqlite3_int64 meta_id,
		    struct gps_utc *utc)
{
	sqlite3_stmt *stmt;
	const char *sql =
		"SELECT a0, a1, tot, wnt, dt_ls"
		" FROM utc_params WHERE metadata_id = ?";
	int rc;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	sqlite3_bind_int64(stmt, 1, meta_id);

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		utc->a0    = sqlite3_column_double(stmt, 0);
		utc->a1    = sqlite3_column_double(stmt, 1);
		utc->tot   = (uint32_t)sqlite3_column_int(stmt, 2);
		utc->wnt   = (uint16_t)sqlite3_column_int(stmt, 3);
		utc->dt_ls = (int8_t)sqlite3_column_int(stmt, 4);
	}

	sqlite3_finalize(stmt);
	return 0;
}

int sqlitedb_read_latest(const char *db_path, struct gps_assist_data *data)
{
	sqlite3 *db;
	sqlite3_int64 meta_id;
	int ret = -1;

	memset(data, 0, sizeof(*data));

	if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL)
	    != SQLITE_OK) {
		fprintf(stderr, "SQLite open error: %s\n",
			sqlite3_errmsg(db));
		sqlite3_close(db);
		return -1;
	}

	if (read_metadata(db, data, &meta_id) != 0)
		goto out;
	if (read_ephemeris(db, meta_id, data) != 0)
		goto out;
	if (read_almanac(db, meta_id, data) != 0)
		goto out;
	if (read_ionosphere(db, meta_id, &data->iono) != 0)
		goto out;
	if (read_utc(db, meta_id, &data->utc) != 0)
		goto out;

	ret = 0;
out:
	sqlite3_close(db);
	return ret;
}

int sqlitedb_store(const char *db_path, const struct gps_assist_data *data,
		   const char *source_name)
{
	sqlite3 *db;
	sqlite3_int64 meta_id;
	char *err = NULL;
	int ret = -1;

	if (sqlite3_open(db_path, &db) != SQLITE_OK) {
		fprintf(stderr, "SQLite open error: %s\n",
			sqlite3_errmsg(db));
		sqlite3_close(db);
		return -1;
	}

	sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
	sqlite3_exec(db, "PRAGMA foreign_keys=ON", NULL, NULL, NULL);

	if (create_schema(db) != 0)
		goto out;

	if (sqlite3_exec(db, "BEGIN", NULL, NULL, &err) != SQLITE_OK) {
		fprintf(stderr, "SQLite BEGIN error: %s\n", err);
		sqlite3_free(err);
		goto out;
	}

	if (insert_metadata(db, data, source_name, &meta_id) != 0) {
		fprintf(stderr, "SQLite: failed to insert metadata\n");
		goto rollback;
	}

	if (data->num_sv > 0) {
		if (insert_ephemeris(db, meta_id, data->sv,
				     data->num_sv, "GPS") != 0) {
			fprintf(stderr, "SQLite: failed to insert GPS ephemeris\n");
			goto rollback;
		}
	}

	if (data->num_qzss > 0) {
		if (insert_ephemeris(db, meta_id, data->qzss,
				     data->num_qzss, "QZSS") != 0) {
			fprintf(stderr, "SQLite: failed to insert QZSS ephemeris\n");
			goto rollback;
		}
	}

	if (data->num_alm > 0) {
		if (insert_almanac(db, meta_id, data->alm,
				   data->num_alm) != 0) {
			fprintf(stderr, "SQLite: failed to insert almanac\n");
			goto rollback;
		}
	}

	if (insert_ionosphere(db, meta_id, &data->iono) != 0) {
		fprintf(stderr, "SQLite: failed to insert ionosphere\n");
		goto rollback;
	}

	if (insert_utc(db, meta_id, &data->utc) != 0) {
		fprintf(stderr, "SQLite: failed to insert UTC params\n");
		goto rollback;
	}

	if (sqlite3_exec(db, "COMMIT", NULL, NULL, &err) != SQLITE_OK) {
		fprintf(stderr, "SQLite COMMIT error: %s\n", err);
		sqlite3_free(err);
		goto rollback;
	}

	fprintf(stderr, "Stored dataset #%lld in %s\n",
		(long long)meta_id, db_path);
	ret = 0;
	goto out;

rollback:
	sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
out:
	sqlite3_close(db);
	return ret;
}
