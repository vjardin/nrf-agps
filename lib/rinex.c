/*
 * rinex.c - RINEX navigation file download and parser
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <curl/curl.h>
#include <zlib.h>

#include "pplog.h"
#include "rinex.h"

/* BKG IGS BRDC combined navigation file (no authentication required) */
#define BRDC_URL_FMT \
	"https://igs.bkg.bund.de/root_ftp/IGS/BRDC/%04d/%03d/" \
	"BRDC00IGS_R_%04d%03d0000_01D_MN.rnx.gz"

#define TMP_PATH_FMT "/tmp/brdc_%04d%03d.rnx.gz"
#define LINE_MAX_LEN 256

/* GPS epoch: January 6, 1980 00:00:00 UTC */
#define GPS_EPOCH_UNIX 315964800L
#define SECS_PER_WEEK  604800L

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
	return fwrite(ptr, size, nmemb, (FILE *)ud);
}

char *rinex_download(int year, int doy, const char *url_override)
{
	char url[512];
	char *path;
	FILE *fp;
	CURL *curl;
	CURLcode res;

	path = malloc(256);
	if (!path)
		return NULL;

	snprintf(path, 256, TMP_PATH_FMT, year, doy);

	if (url_override)
		snprintf(url, sizeof(url), "%s", url_override);
	else
		snprintf(url, sizeof(url), BRDC_URL_FMT,
			 year, doy, year, doy);

	pplog_info("Downloading: %s", url);

	fp = fopen(path, "wb");
	if (!fp) {
		warn("%s", path);
		free(path);
		return NULL;
	}

	curl = curl_easy_init();
	if (!curl) {
		fclose(fp);
		free(path);
		return NULL;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	fclose(fp);

	if (res != CURLE_OK) {
		pplog_err("download failed: %s", curl_easy_strerror(res));
		remove(path);
		free(path);
		return NULL;
	}

	/* Sanity-check file size: a valid combined BRDC .rnx.gz is
	 * typically >100 KB; anything under 1 KB is suspicious. */
	long size = 0;
	fp = fopen(path, "rb");
	if (fp) {
		fseek(fp, 0, SEEK_END);
		size = ftell(fp);
		fclose(fp);
	}
	if (size < 1024)
		pplog_warn("downloaded file is only %ld bytes"
			   " (expected >100 KB)", size);

	pplog_info("Downloaded to: %s (%ld bytes)", path, size);
	return path;
}

/*
 * Parse a RINEX floating-point field.
 * Handles Fortran 'D'/'d' exponent notation (e.g. "1.234D-05").
 */
static double rinex_double(const char *line, int offset, int width)
{
	char buf[32];
	int linelen = (int)strlen(line);

	if (offset >= linelen)
		return 0.0;

	int avail = linelen - offset;
	if (avail > width)
		avail = width;
	if (avail > 31)
		avail = 31;

	memcpy(buf, line + offset, avail);
	buf[avail] = '\0';

	for (int i = 0; i < avail; i++) {
		if (buf[i] == 'D' || buf[i] == 'd')
			buf[i] = 'E';
	}

	return strtod(buf, NULL);
}

static void parse_iono_hdr(const char *line, struct gps_iono *iono)
{
	char buf[256], id[8];
	double v[4];

	strncpy(buf, line, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	for (int i = 0; buf[i]; i++)
		if (buf[i] == 'D' || buf[i] == 'd')
			buf[i] = 'E';

	if (sscanf(buf, "%4s %lf %lf %lf %lf", id,
		   &v[0], &v[1], &v[2], &v[3]) != 5)
		return;

	if (strcmp(id, "GPSA") == 0)
		memcpy(iono->alpha, v, sizeof(v));
	else if (strcmp(id, "GPSB") == 0)
		memcpy(iono->beta, v, sizeof(v));
}

static void parse_utc_hdr(const char *line, struct gps_utc *utc)
{
	char buf[256], id[8];
	double a0, a1;
	int tot, wnt;

	strncpy(buf, line, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	for (int i = 0; buf[i]; i++)
		if (buf[i] == 'D' || buf[i] == 'd')
			buf[i] = 'E';

	if (sscanf(buf, "%4s %lf %lf %d %d", id,
		   &a0, &a1, &tot, &wnt) != 5)
		return;

	if (strcmp(id, "GPUT") == 0) {
		utc->a0  = a0;
		utc->a1  = a1;
		utc->tot = (uint32_t)tot;
		utc->wnt = (uint16_t)wnt;
	}
}

static void parse_leap_hdr(const char *line, struct gps_utc *utc)
{
	int ls;

	if (sscanf(line, " %d", &ls) == 1)
		utc->dt_ls = (int8_t)ls;
}

static void parse_sv_epoch(const char *line, struct gps_ephemeris *eph)
{
	char prn_s[3];
	int year, month, day, hour, min, sec;
	struct tm tm;

	/* PRN */
	prn_s[0] = line[1];
	prn_s[1] = line[2];
	prn_s[2] = '\0';
	eph->prn = (uint8_t)atoi(prn_s);

	/* Epoch → toc */
	sscanf(line + 3, " %d %d %d %d %d %d",
	       &year, &month, &day, &hour, &min, &sec);

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = year - 1900;
	tm.tm_mon  = month - 1;
	tm.tm_mday = day;
	tm.tm_hour = hour;
	tm.tm_min  = min;
	tm.tm_sec  = sec;
	time_t t = timegm(&tm);

	long gps_sec = (long)(t - GPS_EPOCH_UNIX);
	eph->week = (uint16_t)(gps_sec / SECS_PER_WEEK);
	eph->toc  = (uint32_t)(gps_sec % SECS_PER_WEEK);

	/* Clock parameters: 3 × D19.12 starting at column 23 */
	eph->af0 = rinex_double(line, 23, 19);
	eph->af1 = rinex_double(line, 42, 19);
	eph->af2 = rinex_double(line, 61, 19);
}

/*
 * Parse one broadcast orbit line (7 orbit lines per GPS record).
 *
 * GPS RINEX v3 orbit data layout:
 *   Line 0: IODE    Crs     delta_n  M0
 *   Line 1: Cuc     e       Cus      sqrt_A
 *   Line 2: toe     Cic     OMEGA0   Cis
 *   Line 3: i0      Crc     omega    OMEGA_DOT
 *   Line 4: IDOT    L2codes week     L2P_flag
 *   Line 5: accuracy health  TGD     IODC
 *   Line 6: tx_time  fit_interval  (spare) (spare)
 *
 * Each line: 4 spaces + 4 × D19.12 values.
 */
static void parse_orbit(const char *line, int idx, struct gps_ephemeris *eph)
{
	double v[4];

	for (int i = 0; i < 4; i++)
		v[i] = rinex_double(line, 4 + i * 19, 19);

	switch (idx) {
	case 0:
		eph->iode    = (uint8_t)v[0];
		eph->crs     = v[1];
		eph->delta_n = v[2];
		eph->m0      = v[3];
		break;
	case 1:
		eph->cuc    = v[0];
		eph->e      = v[1];
		eph->cus    = v[2];
		eph->sqrt_a = v[3];
		break;
	case 2:
		eph->toe    = (uint32_t)v[0];
		eph->cic    = v[1];
		eph->omega0 = v[2];
		eph->cis    = v[3];
		break;
	case 3:
		eph->i0        = v[0];
		eph->crc       = v[1];
		eph->omega     = v[2];
		eph->omega_dot = v[3];
		break;
	case 4:
		eph->idot = v[0];
		/* L2 codes = v[1], skip */
		eph->week = (uint16_t)v[2];
		/* L2P flag = v[3], skip */
		break;
	case 5:
		/* SV accuracy = v[0], skip */
		eph->health = (uint8_t)v[1];
		eph->tgd    = v[2];
		eph->iodc   = (uint16_t)v[3];
		break;
	case 6:
		/* transmission time = v[0], fit interval = v[1] */
		break;
	}
}

/*
 * Keep only the most recent ephemeris per PRN (highest toe).
 */
static void save_ephemeris(struct gps_assist_data *data,
			   const struct gps_ephemeris *eph)
{
	if (eph->prn < 1 || eph->prn > GPS_MAX_SATS)
		return;

	for (int i = 0; i < data->num_sv; i++) {
		if (data->sv[i].prn == eph->prn) {
			if (eph->toe >= data->sv[i].toe)
				data->sv[i] = *eph;
			return;
		}
	}

	if (data->num_sv < GPS_MAX_SATS) {
		data->sv[data->num_sv] = *eph;
		data->num_sv++;
	}
}

static void save_qzss_ephemeris(struct gps_assist_data *data,
				const struct gps_ephemeris *eph)
{
	if (eph->prn < QZSS_PRN_OFFSET + 1 ||
	    eph->prn > QZSS_PRN_OFFSET + QZSS_MAX_SATS)
		return;

	for (int i = 0; i < data->num_qzss; i++) {
		if (data->qzss[i].prn == eph->prn) {
			if (eph->toe >= data->qzss[i].toe)
				data->qzss[i] = *eph;
			return;
		}
	}

	if (data->num_qzss < QZSS_MAX_SATS) {
		data->qzss[data->num_qzss] = *eph;
		data->num_qzss++;
	}
}

int rinex_parse(const char *path, struct gps_assist_data *out)
{
	gzFile gz;
	char line[LINE_MAX_LEN];
	int in_header = 1;
	int orbit_line = -1;
	int is_gps = 0;
	int is_qzss = 0;
	struct gps_ephemeris eph;

	memset(out, 0, sizeof(*out));

	gz = gzopen(path, "rb");
	if (!gz) {
		warn("%s", path);
		return -1;
	}

	/* --- Header --- */
	while (gzgets(gz, line, sizeof(line))) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' ||
				   line[len - 1] == '\r'))
			line[--len] = '\0';

		if (len < 60)
			continue;

		const char *label = line + 60;

		if (strstr(label, "END OF HEADER")) {
			in_header = 0;
			break;
		}
		if (strstr(label, "RINEX VERSION")) {
			double ver = strtod(line, NULL);
			if (ver < 3.0) {
				pplog_err("RINEX v%.0f not supported"
					  " (need 3+)", ver);
				gzclose(gz);
				return -1;
			}
		} else if (strstr(label, "IONOSPHERIC CORR")) {
			parse_iono_hdr(line, &out->iono);
		} else if (strstr(label, "TIME SYSTEM CORR")) {
			parse_utc_hdr(line, &out->utc);
		} else if (strstr(label, "LEAP SECONDS")) {
			parse_leap_hdr(line, &out->utc);
		}
	}

	if (in_header) {
		pplog_err("unexpected end of file in header");
		gzclose(gz);
		return -1;
	}

	/* --- Data records --- */
	while (gzgets(gz, line, sizeof(line))) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' ||
				   line[len - 1] == '\r'))
			line[--len] = '\0';

		if (len == 0)
			continue;

		/* RINEX 4 record headers start with '>' */
		if (line[0] == '>')
			continue;

		/* New satellite record (starts with a letter) */
		if (line[0] >= 'A' && line[0] <= 'Z') {
			/* Save previous record if complete */
			if (is_gps && orbit_line == 7)
				save_ephemeris(out, &eph);
			else if (is_qzss && orbit_line == 7)
				save_qzss_ephemeris(out, &eph);

			is_gps  = (line[0] == 'G');
			is_qzss = (line[0] == 'J');
			orbit_line = 0;

			if (is_gps || is_qzss) {
				memset(&eph, 0, sizeof(eph));
				parse_sv_epoch(line, &eph);
				if (is_qzss)
					eph.prn += QZSS_PRN_OFFSET;
				if (out->gps_week == 0)
					out->gps_week = eph.week;
			}
			continue;
		}

		/* Orbit data line (starts with spaces) */
		if ((is_gps || is_qzss) &&
		    orbit_line >= 0 && orbit_line < 7) {
			parse_orbit(line, orbit_line, &eph);
			orbit_line++;
		}
	}

	/* Save last record */
	if (is_gps && orbit_line == 7)
		save_ephemeris(out, &eph);
	else if (is_qzss && orbit_line == 7)
		save_qzss_ephemeris(out, &eph);

	gzclose(gz);
	if (out->num_qzss)
		pplog_info("Parsed %d GPS + %d QZSS satellites",
			   out->num_sv, out->num_qzss);
	else
		pplog_info("Parsed %d GPS satellites", out->num_sv);
	return 0;
}
