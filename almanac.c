/*
 * almanac.c - GPS almanac download and parser (SEM/YUMA formats)
 *
 * SEM format: compact, 3 values per orbit line, semi-circles for angles.
 * YUMA format: labeled lines, values in radians (converted to semi-circles).
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "almanac.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TMP_SEM_PATH "/tmp/current_sem.al3"
#define LINE_MAX_LEN 256

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
	return fwrite(ptr, size, nmemb, (FILE *)ud);
}

/*
 * SEM almanac format (USCG NAVCEN):
 *
 * Header:
 *   num_records  filename
 *   gps_week  toa
 *
 * Per satellite (blank line separator):
 *   PRN
 *   SVN
 *   Average URA
 *   e  delta_i(sc)  omega_dot(sc/s)
 *   sqrt_a(m^0.5)  omega0(sc)  omega(sc)
 *   m0(sc)  af0(s)  af1(s/s)
 *   health
 *   satellite_config
 */
int almanac_parse_sem(const char *path, struct gps_assist_data *data)
{
	FILE *fp;
	char line[LINE_MAX_LEN];
	int num_records;
	uint16_t week;
	uint32_t toa;

	fp = fopen(path, "r");
	if (!fp) {
		perror("fopen");
		return -1;
	}

	/* Header line 1: num_records  filename */
	if (!fgets(line, sizeof(line), fp) ||
	    sscanf(line, "%d", &num_records) != 1) {
		fclose(fp);
		return -1;
	}

	/* Header line 2: gps_week  toa */
	if (!fgets(line, sizeof(line), fp) ||
	    sscanf(line, "%hu %u", &week, &toa) != 2) {
		fclose(fp);
		return -1;
	}

	data->num_alm = 0;

	for (int n = 0; n < num_records && data->num_alm < GPS_MAX_SATS; n++) {
		struct gps_almanac *alm = &data->alm[data->num_alm];
		int prn, svn, ura, health, config;
		double vals[3];

		/* Skip blank lines before each record */
		while (fgets(line, sizeof(line), fp)) {
			char *p = line;
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p != '\n' && *p != '\r' && *p != '\0')
				break;
		}

		/* PRN */
		if (sscanf(line, "%d", &prn) != 1)
			break;

		/* SVN (unused) */
		if (!fgets(line, sizeof(line), fp) ||
		    sscanf(line, "%d", &svn) != 1)
			break;
		(void)svn;

		/* URA (unused) */
		if (!fgets(line, sizeof(line), fp) ||
		    sscanf(line, "%d", &ura) != 1)
			break;
		(void)ura;

		memset(alm, 0, sizeof(*alm));

		/* e, delta_i(sc), omega_dot(sc/s) */
		if (!fgets(line, sizeof(line), fp) ||
		    sscanf(line, "%lf %lf %lf",
			   &vals[0], &vals[1], &vals[2]) != 3)
			break;
		alm->e         = vals[0];
		alm->delta_i   = vals[1];
		alm->omega_dot = vals[2];

		/* sqrt_a, omega0(sc), omega(sc) */
		if (!fgets(line, sizeof(line), fp) ||
		    sscanf(line, "%lf %lf %lf",
			   &vals[0], &vals[1], &vals[2]) != 3)
			break;
		alm->sqrt_a = vals[0];
		alm->omega0 = vals[1];
		alm->omega  = vals[2];

		/* m0(sc), af0(s), af1(s/s) */
		if (!fgets(line, sizeof(line), fp) ||
		    sscanf(line, "%lf %lf %lf",
			   &vals[0], &vals[1], &vals[2]) != 3)
			break;
		alm->m0  = vals[0];
		alm->af0 = vals[1];
		alm->af1 = vals[2];

		/* health */
		if (!fgets(line, sizeof(line), fp) ||
		    sscanf(line, "%d", &health) != 1)
			break;

		/* satellite config (unused) */
		if (!fgets(line, sizeof(line), fp) ||
		    sscanf(line, "%d", &config) != 1)
			break;
		(void)config;

		if (prn < 1 || prn > GPS_MAX_SATS)
			continue;

		alm->prn    = (uint8_t)prn;
		alm->health = (uint8_t)health;
		alm->ioda   = 0;
		alm->week   = week;
		alm->toa    = toa;

		data->num_alm++;
	}

	fclose(fp);
	fprintf(stderr, "Parsed %d almanac entries (SEM)\n", data->num_alm);
	return 0;
}

/*
 * YUMA almanac format (labeled lines, radians):
 *
 * ******** Week NNN almanac for PRN-XX ********
 * ID:                         XX
 * Health:                     XXX
 * Eccentricity:               X.XXXXXXXXXX
 * Time of Applicability(s):   XXXXXX.XXXX
 * Orbital Inclination(rad):   X.XXXXXXXXXX
 * Rate of Right Ascen(r/s):   X.XXXXXXXXXX
 * SQRT(A)  (m 1/2):           XXXX.XXXXXX
 * Right Ascen at Week(rad):   X.XXXXXXXXXX
 * Argument of Perigee(rad):   X.XXXXXXXXXX
 * Mean Anom(rad):              X.XXXXXXXXXX
 * Af0(s):                      X.XXXXXXXXXX
 * Af1(s/s):                    X.XXXXXXXXXX
 * week:                        XXXX
 */
static double yuma_value(const char *line)
{
	const char *colon = strchr(line, ':');

	if (!colon)
		return 0.0;
	return strtod(colon + 1, NULL);
}

static inline double rad2sc(double rad)
{
	return rad / M_PI;
}

int almanac_parse_yuma(const char *path, struct gps_assist_data *data)
{
	FILE *fp;
	char line[LINE_MAX_LEN];

	fp = fopen(path, "r");
	if (!fp) {
		perror("fopen");
		return -1;
	}

	data->num_alm = 0;

	while (fgets(line, sizeof(line), fp) &&
	       data->num_alm < GPS_MAX_SATS) {
		/* Look for separator line starting with *** */
		if (strncmp(line, "***", 3) != 0)
			continue;

		struct gps_almanac *alm = &data->alm[data->num_alm];

		memset(alm, 0, sizeof(*alm));

		/* ID */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->prn = (uint8_t)yuma_value(line);

		/* Health */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->health = (uint8_t)yuma_value(line);

		/* Eccentricity (dimensionless) */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->e = yuma_value(line);

		/* Time of Applicability (seconds) */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->toa = (uint32_t)yuma_value(line);

		/* Orbital Inclination (rad -> delta_i in sc) */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->delta_i = rad2sc(yuma_value(line)) - 0.3;

		/* Rate of Right Ascension (rad/s -> sc/s) */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->omega_dot = rad2sc(yuma_value(line));

		/* SQRT(A) (m^0.5) */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->sqrt_a = yuma_value(line);

		/* Right Ascension at Week (rad -> sc) */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->omega0 = rad2sc(yuma_value(line));

		/* Argument of Perigee (rad -> sc) */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->omega = rad2sc(yuma_value(line));

		/* Mean Anomaly (rad -> sc) */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->m0 = rad2sc(yuma_value(line));

		/* Af0 (seconds) */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->af0 = yuma_value(line);

		/* Af1 (s/s) */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->af1 = yuma_value(line);

		/* week */
		if (!fgets(line, sizeof(line), fp))
			break;
		alm->week = (uint16_t)yuma_value(line);

		if (alm->prn < 1 || alm->prn > GPS_MAX_SATS)
			continue;

		alm->ioda = 0;
		data->num_alm++;
	}

	fclose(fp);
	fprintf(stderr, "Parsed %d almanac entries (YUMA)\n", data->num_alm);
	return 0;
}

int almanac_download_sem(struct gps_assist_data *data)
{
	FILE *fp;
	CURL *curl;
	CURLcode res;

	fp = fopen(TMP_SEM_PATH, "wb");
	if (!fp) {
		perror("fopen");
		return -1;
	}

	curl = curl_easy_init();
	if (!curl) {
		fclose(fp);
		return -1;
	}

	fprintf(stderr, "Downloading SEM almanac: %s\n", ALMANAC_SEM_URL);

	curl_easy_setopt(curl, CURLOPT_URL, ALMANAC_SEM_URL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	fclose(fp);

	if (res != CURLE_OK) {
		fprintf(stderr, "SEM download failed: %s\n",
			curl_easy_strerror(res));
		remove(TMP_SEM_PATH);
		return -1;
	}

	int ret = almanac_parse_sem(TMP_SEM_PATH, data);

	remove(TMP_SEM_PATH);
	return ret;
}
