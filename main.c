/*
 * main.c - rinex_dl CLI entry point
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <curl/curl.h>

#include "gps_assist.h"
#include "rinex.h"
#include "codegen.h"

static int day_of_year(int year, int month, int day)
{
	static const int cumdays[] = {
		0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
	};
	int doy = cumdays[month - 1] + day;

	if (month > 2 && (year % 4 == 0) &&
	    (year % 100 != 0 || year % 400 == 0))
		doy++;

	return doy;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Download GPS broadcast ephemeris and generate A-GPS C source.\n"
		"\n"
		"Options:\n"
		"  -d YYYY-MM-DD  Date to download (default: yesterday)\n"
		"  -o PREFIX      Output file prefix (default: gps_assist_data)\n"
		"  -u URL         Custom RINEX navigation file URL\n"
		"  -l LAT,LON    Approximate reference location (default: Paris Notre Dame)\n"
		"  -f FILE        Parse local RINEX file instead of downloading\n"
		"  -h             Show this help\n",
		prog);
}

int main(int argc, char **argv)
{
	int year = 0, month = 0, day = 0, doy = 0;
	const char *output = "gps_assist_data";
	const char *url = NULL;
	const char *local_file = NULL;
	double ref_lat = 48.8530;   /* Paris Notre Dame */
	double ref_lon = 2.3498;
	int opt;

	while ((opt = getopt(argc, argv, "d:l:o:u:f:h")) != -1) {
		switch (opt) {
		case 'd':
			if (sscanf(optarg, "%d-%d-%d",
				   &year, &month, &day) != 3) {
				fprintf(stderr, "Bad date format: %s\n",
					optarg);
				return 1;
			}
			if (month < 1 || month > 12 ||
			    day < 1 || day > 31) {
				fprintf(stderr, "Invalid date: %s\n", optarg);
				return 1;
			}
			break;
		case 'l':
			if (sscanf(optarg, "%lf,%lf",
				   &ref_lat, &ref_lon) != 2) {
				fprintf(stderr, "Bad location format: %s "
					"(expected LAT,LON)\n", optarg);
				return 1;
			}
			if (ref_lat < -90 || ref_lat > 90 ||
			    ref_lon < -180 || ref_lon > 180) {
				fprintf(stderr, "Invalid coordinates: %s\n",
					optarg);
				return 1;
			}
			break;
		case 'o':
			output = optarg;
			break;
		case 'u':
			url = optarg;
			break;
		case 'f':
			local_file = optarg;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	/* Default: yesterday (today's BRDC may not be complete yet) */
	if (year == 0 && !local_file) {
		time_t t = time(NULL) - 86400;
		struct tm *tm = gmtime(&t);

		year  = tm->tm_year + 1900;
		month = tm->tm_mon + 1;
		day   = tm->tm_mday;
	}

	if (!local_file)
		doy = day_of_year(year, month, day);

	curl_global_init(CURL_GLOBAL_DEFAULT);

	/* Obtain RINEX file */
	char *path = NULL;

	if (local_file) {
		path = strdup(local_file);
	} else {
		fprintf(stderr, "Fetching BRDC data for %04d-%02d-%02d "
			"(DOY %03d)\n", year, month, day, doy);
		path = rinex_download(year, doy, url);
	}

	if (!path) {
		fprintf(stderr, "Failed to obtain RINEX file\n");
		curl_global_cleanup();
		return 1;
	}

	/* Parse */
	struct gps_assist_data data;

	if (rinex_parse(path, &data) != 0) {
		fprintf(stderr, "Failed to parse RINEX file\n");
		if (!local_file)
			remove(path);
		free(path);
		curl_global_cleanup();
		return 1;
	}

	if (data.num_sv == 0) {
		fprintf(stderr, "No GPS satellites found in file\n");
		if (!local_file)
			remove(path);
		free(path);
		curl_global_cleanup();
		return 1;
	}

	data.timestamp = (uint32_t)time(NULL);
	data.location.latitude  = ref_lat;
	data.location.longitude = ref_lon;
	data.location.altitude  = 0;
	data.location.valid     = 1;

	/* Generate C source */
	const char *source = strrchr(path, '/');
	source = source ? source + 1 : path;

	if (codegen_write(output, &data, source) != 0) {
		fprintf(stderr, "Failed to generate output\n");
		if (!local_file)
			remove(path);
		free(path);
		curl_global_cleanup();
		return 1;
	}

	fprintf(stderr, "Done: %d satellites, GPS week %u\n",
		data.num_sv, data.gps_week);

	if (!local_file)
		remove(path);
	free(path);
	curl_global_cleanup();
	return 0;
}
