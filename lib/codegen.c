/*
 * codegen.c - C source file generator for GPS assistance data
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <err.h>
#include <stdio.h>
#include <time.h>

#include "codegen.h"

static void emit_almanac(FILE *fp, const struct gps_almanac *alm)
{
	fprintf(fp, "\t\t{\n");
	fprintf(fp, "\t\t\t.prn = %u,\n", alm->prn);
	fprintf(fp, "\t\t\t.health = %u,\n", alm->health);
	fprintf(fp, "\t\t\t.ioda = %u,\n", alm->ioda);
	fprintf(fp, "\t\t\t.week = %u,\n", alm->week);
	fprintf(fp, "\t\t\t.toa = %u,\n", alm->toa);
	fprintf(fp, "\t\t\t.e = %.12e,\n", alm->e);
	fprintf(fp, "\t\t\t.delta_i = %.12e,\n", alm->delta_i);
	fprintf(fp, "\t\t\t.omega_dot = %.12e,\n", alm->omega_dot);
	fprintf(fp, "\t\t\t.sqrt_a = %.12e,\n", alm->sqrt_a);
	fprintf(fp, "\t\t\t.omega0 = %.12e,\n", alm->omega0);
	fprintf(fp, "\t\t\t.omega = %.12e,\n", alm->omega);
	fprintf(fp, "\t\t\t.m0 = %.12e,\n", alm->m0);
	fprintf(fp, "\t\t\t.af0 = %.12e,\n", alm->af0);
	fprintf(fp, "\t\t\t.af1 = %.12e,\n", alm->af1);
	fprintf(fp, "\t\t},\n");
}

static void emit_ephemeris(FILE *fp, const struct gps_ephemeris *sv)
{
	fprintf(fp, "\t\t{\n");
	fprintf(fp, "\t\t\t.prn = %u,\n", sv->prn);
	fprintf(fp, "\t\t\t.health = %u,\n", sv->health);
	fprintf(fp, "\t\t\t.iodc = %u,\n", sv->iodc);
	fprintf(fp, "\t\t\t.iode = %u,\n", sv->iode);
	fprintf(fp, "\t\t\t.week = %u,\n", sv->week);
	fprintf(fp, "\t\t\t.toe = %u,\n", sv->toe);
	fprintf(fp, "\t\t\t.toc = %u,\n", sv->toc);
	fprintf(fp, "\t\t\t.af0 = %.12e,\n", sv->af0);
	fprintf(fp, "\t\t\t.af1 = %.12e,\n", sv->af1);
	fprintf(fp, "\t\t\t.af2 = %.12e,\n", sv->af2);
	fprintf(fp, "\t\t\t.sqrt_a = %.12e,\n", sv->sqrt_a);
	fprintf(fp, "\t\t\t.e = %.12e,\n", sv->e);
	fprintf(fp, "\t\t\t.i0 = %.12e,\n", sv->i0);
	fprintf(fp, "\t\t\t.omega0 = %.12e,\n", sv->omega0);
	fprintf(fp, "\t\t\t.omega = %.12e,\n", sv->omega);
	fprintf(fp, "\t\t\t.m0 = %.12e,\n", sv->m0);
	fprintf(fp, "\t\t\t.delta_n = %.12e,\n", sv->delta_n);
	fprintf(fp, "\t\t\t.omega_dot = %.12e,\n", sv->omega_dot);
	fprintf(fp, "\t\t\t.idot = %.12e,\n", sv->idot);
	fprintf(fp, "\t\t\t.cuc = %.12e,\n", sv->cuc);
	fprintf(fp, "\t\t\t.cus = %.12e,\n", sv->cus);
	fprintf(fp, "\t\t\t.crc = %.12e,\n", sv->crc);
	fprintf(fp, "\t\t\t.crs = %.12e,\n", sv->crs);
	fprintf(fp, "\t\t\t.cic = %.12e,\n", sv->cic);
	fprintf(fp, "\t\t\t.cis = %.12e,\n", sv->cis);
	fprintf(fp, "\t\t\t.tgd = %.12e,\n", sv->tgd);
	fprintf(fp, "\t\t},\n");
}

int codegen_write(const char *prefix, const struct gps_assist_data *data,
		  const char *source_name)
{
	char path[512];
	FILE *fp;
	time_t now = time(NULL);
	struct tm *tm = gmtime(&now);

	snprintf(path, sizeof(path), "%s.c", prefix);

	fp = fopen(path, "w");
	if (!fp) {
		warn("%s", path);
		return -1;
	}

	fprintf(fp,
		"/* Auto-generated GPS assistance data\n"
		" * Generated: %04d-%02d-%02dT%02d:%02d:%02dZ\n",
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);
	if (source_name)
		fprintf(fp, " * Source: %s\n", source_name);
	fprintf(fp,
		" * Satellites: %d\n"
		" * GPS week: %u\n"
		" *\n"
		" * DO NOT EDIT - regenerate with rinex_dl\n"
		" */\n\n",
		data->num_sv, data->gps_week);

	fprintf(fp, "#include \"gps_assist.h\"\n\n");

	fprintf(fp, "const struct gps_assist_data gps_assist = {\n");
	fprintf(fp, "\t.timestamp = %uU,\n", data->timestamp);
	fprintf(fp, "\t.gps_week = %u,\n", data->gps_week);
	fprintf(fp, "\t.num_sv = %u,\n", data->num_sv);

	/* Ionospheric model */
	fprintf(fp, "\t.iono = {\n");
	fprintf(fp, "\t\t.alpha = { %.12e, %.12e, %.12e, %.12e },\n",
		data->iono.alpha[0], data->iono.alpha[1],
		data->iono.alpha[2], data->iono.alpha[3]);
	fprintf(fp, "\t\t.beta = { %.12e, %.12e, %.12e, %.12e },\n",
		data->iono.beta[0], data->iono.beta[1],
		data->iono.beta[2], data->iono.beta[3]);
	fprintf(fp, "\t},\n");

	/* UTC parameters */
	fprintf(fp, "\t.utc = {\n");
	fprintf(fp, "\t\t.a0 = %.12e,\n", data->utc.a0);
	fprintf(fp, "\t\t.a1 = %.12e,\n", data->utc.a1);
	fprintf(fp, "\t\t.tot = %u,\n", data->utc.tot);
	fprintf(fp, "\t\t.wnt = %u,\n", data->utc.wnt);
	fprintf(fp, "\t\t.dt_ls = %d,\n", data->utc.dt_ls);
	fprintf(fp, "\t},\n");

	/* Location */
	if (data->location.valid) {
		fprintf(fp, "\t.location = {\n");
		fprintf(fp, "\t\t.latitude = %.4f,\n", data->location.latitude);
		fprintf(fp, "\t\t.longitude = %.4f,\n", data->location.longitude);
		fprintf(fp, "\t\t.altitude = %d,\n", data->location.altitude);
		fprintf(fp, "\t\t.valid = 1,\n");
		fprintf(fp, "\t},\n");
	}

	/* Almanac entries (parsed SEM/YUMA) */
	if (data->num_alm > 0) {
		fprintf(fp, "\t.num_alm = %u,\n", data->num_alm);
		fprintf(fp, "\t.alm = {\n");
		for (int i = 0; i < data->num_alm; i++)
			emit_almanac(fp, &data->alm[i]);
		fprintf(fp, "\t},\n");
	}

	/* Satellite ephemerides */
	fprintf(fp, "\t.sv = {\n");
	for (int i = 0; i < data->num_sv; i++)
		emit_ephemeris(fp, &data->sv[i]);
	fprintf(fp, "\t},\n");

	/* QZSS ephemerides */
	if (data->num_qzss > 0) {
		fprintf(fp, "\t.num_qzss = %u,\n", data->num_qzss);
		fprintf(fp, "\t.qzss = {\n");
		for (int i = 0; i < data->num_qzss; i++)
			emit_ephemeris(fp, &data->qzss[i]);
		fprintf(fp, "\t},\n");
	}

	fprintf(fp, "};\n");

	fclose(fp);
	fprintf(stderr, "Generated: %s\n", path);
	return 0;
}
