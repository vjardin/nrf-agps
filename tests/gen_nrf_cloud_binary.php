<?php
/*
 * gen_nrf_cloud_binary.php - Generate nRF Cloud binary for C cross-validation
 *
 * Creates a test SQLite database with known values and generates
 * the nRF Cloud binary A-GNSS response to a file.
 *
 * Usage: php tests/gen_nrf_cloud_binary.php <output_file>
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

declare(strict_types=1);

require_once __DIR__ . '/../php/agnss.php';
require_once __DIR__ . '/../php/nrf_cloud.php';

if ($argc < 2) {
	fprintf(STDERR, "Usage: %s <output_file>\n", $argv[0]);
	exit(1);
}

/* Create temp database with known test values */
$dbPath = tempnam(sys_get_temp_dir(), 'gen_nrf_cloud_');
$db = new PDO('sqlite:' . $dbPath, null, null, [
	PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
]);

$db->exec("
	CREATE TABLE metadata (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		timestamp INTEGER NOT NULL,
		gps_week INTEGER NOT NULL,
		source_name TEXT,
		ref_latitude REAL,
		ref_longitude REAL,
		ref_altitude INTEGER,
		created_at TEXT DEFAULT (datetime('now'))
	);
	CREATE TABLE ephemeris (
		metadata_id INTEGER NOT NULL REFERENCES metadata(id),
		constellation TEXT NOT NULL DEFAULT 'GPS',
		prn INTEGER NOT NULL,
		health INTEGER NOT NULL,
		iodc INTEGER NOT NULL,
		iode INTEGER NOT NULL,
		week INTEGER NOT NULL,
		toe INTEGER NOT NULL,
		toc INTEGER NOT NULL,
		af0 REAL NOT NULL,
		af1 REAL NOT NULL,
		af2 REAL NOT NULL,
		sqrt_a REAL NOT NULL,
		e REAL NOT NULL,
		i0 REAL NOT NULL,
		omega0 REAL NOT NULL,
		omega REAL NOT NULL,
		m0 REAL NOT NULL,
		delta_n REAL NOT NULL,
		omega_dot REAL NOT NULL,
		idot REAL NOT NULL,
		cuc REAL NOT NULL,
		cus REAL NOT NULL,
		crc REAL NOT NULL,
		crs REAL NOT NULL,
		cic REAL NOT NULL,
		cis REAL NOT NULL,
		tgd REAL NOT NULL,
		PRIMARY KEY (metadata_id, constellation, prn)
	);
	CREATE TABLE almanac (
		metadata_id INTEGER NOT NULL REFERENCES metadata(id),
		prn INTEGER NOT NULL,
		health INTEGER NOT NULL,
		ioda INTEGER NOT NULL,
		week INTEGER NOT NULL,
		toa INTEGER NOT NULL,
		e REAL NOT NULL,
		delta_i REAL NOT NULL,
		omega_dot REAL NOT NULL,
		sqrt_a REAL NOT NULL,
		omega0 REAL NOT NULL,
		omega REAL NOT NULL,
		m0 REAL NOT NULL,
		af0 REAL NOT NULL,
		af1 REAL NOT NULL,
		PRIMARY KEY (metadata_id, prn)
	);
	CREATE TABLE ionosphere (
		metadata_id INTEGER PRIMARY KEY REFERENCES metadata(id),
		alpha0 REAL NOT NULL,
		alpha1 REAL NOT NULL,
		alpha2 REAL NOT NULL,
		alpha3 REAL NOT NULL,
		beta0 REAL NOT NULL,
		beta1 REAL NOT NULL,
		beta2 REAL NOT NULL,
		beta3 REAL NOT NULL
	);
	CREATE TABLE utc_params (
		metadata_id INTEGER PRIMARY KEY REFERENCES metadata(id),
		a0 REAL NOT NULL,
		a1 REAL NOT NULL,
		tot INTEGER NOT NULL,
		wnt INTEGER NOT NULL,
		dt_ls INTEGER NOT NULL
	);
");

$db->exec("
	INSERT INTO metadata (timestamp, gps_week, source_name,
		ref_latitude, ref_longitude, ref_altitude)
	VALUES (1773064800, 2409, 'test.rnx', 48.853, 2.3498, 35);
");

$db->exec("
	INSERT INTO ephemeris (metadata_id, constellation, prn,
		health, iodc, iode, week, toe, toc,
		af0, af1, af2, sqrt_a, e, i0,
		omega0, omega, m0, delta_n, omega_dot, idot,
		cuc, cus, crc, crs, cic, cis, tgd)
	VALUES
	(1, 'GPS', 1, 0, 933, 165, 2409, 396000, 396000,
		-2.578310668468e-05, -2.842170943040e-14, 0.0,
		5153.648925781250, 5.483717424795e-03, 9.755508981940e-01,
		-2.025233279608, -1.722784098283, 1.296410068666,
		4.200289988741e-09, -7.793218395069e-09, -2.078767516450e-10,
		-2.935528755188e-06, 8.162856101990e-06, 222.65625, -48.0625,
		1.043081283569e-07, 1.303851604462e-07, -1.117587089539e-08),
	(1, 'QZSS', 193, 0, 10, 10, 2409, 396000, 396000,
		-1.0e-06, 0.0, 0.0,
		6493.296875, 0.075, 0.7330382858,
		2.356194490, -1.570796327, 0.785398163,
		3.0e-09, -6.0e-09, 1.0e-10,
		0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
");

$db->exec("
	INSERT INTO almanac (metadata_id, prn, health, ioda,
		week, toa, e, delta_i, omega_dot, sqrt_a,
		omega0, omega, m0, af0, af1)
	VALUES (1, 1, 0, 2, 2409, 405504,
		5.48e-03, -1.54e-03, -2.48e-09, 5153.65,
		-0.644, -0.548, 0.412, -2.58e-05, -2.84e-14);
");

$db->exec("
	INSERT INTO ionosphere (metadata_id,
		alpha0, alpha1, alpha2, alpha3,
		beta0, beta1, beta2, beta3)
	VALUES (1,
		1.118e-08, 0.0, -5.961e-08, 0.0,
		9.011e+04, 0.0, -1.966e+05, 0.0);
");

$db->exec("
	INSERT INTO utc_params (metadata_id, a0, a1, tot, wnt, dt_ls)
	VALUES (1, 1.863e-09, 4.441e-15, 233472, 2409, 18);
");

/* Build binary response with all types */
/** @var list<int> $types */
$types = [
	NRF_CLOUD_AGNSS_UTC,
	NRF_CLOUD_AGNSS_EPHEMERIS,
	NRF_CLOUD_AGNSS_ALMANAC,
	NRF_CLOUD_AGNSS_KLOBUCHAR,
	NRF_CLOUD_AGNSS_SYSTEM_CLOCK,
	NRF_CLOUD_AGNSS_LOCATION,
	NRF_CLOUD_AGNSS_INTEGRITY,
	NRF_CLOUD_AGNSS_QZSS_EPHEMERIS,
];

$payload = nrf_cloud_build_response($dbPath, $types);

unlink($dbPath);

$written = file_put_contents($argv[1], $payload);
if ($written === false) {
	fprintf(STDERR, "Failed to write %s\n", $argv[1]);
	exit(1);
}

fprintf(STDERR, "gen_nrf_cloud_binary: wrote %d bytes to %s\n",
	strlen($payload), $argv[1]);
