<?php
/*
 * test_php_api.php - PHP REST API unit tests
 *
 * Tests agnss_query() functions directly against a temp SQLite database.
 * Run: php tests/test_php_api.php
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

declare(strict_types=1);

require_once __DIR__ . '/../php/agnss.php';

$tests_run = 0;
$tests_passed = 0;

function assert_test(bool $cond, string $msg): void
{
	global $tests_run, $tests_passed;
	$tests_run++;
	if (!$cond) {
		$bt = debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS, 1);
		fprintf(STDERR, "  FAIL: %s (line %d)\n", $msg, $bt[0]['line'] ?? 0);
	} else {
		$tests_passed++;
	}
}

function assert_float(float $a, float $b, string $msg): void
{
	assert_test(abs($a - $b) < 1e-15, $msg);
}

/* Create and populate a temp database */
function create_test_db(): string
{
	$path = tempnam(sys_get_temp_dir(), 'test_php_api_');
	$db = new PDO('sqlite:' . $path, null, null, [
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

	/* Insert test dataset */
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
		(1, 'GPS', 3, 0, 42, 42, 2409, 388800, 388800,
			3.510899841785e-04, 0.0, 0.0,
			5153.600097656250, 1.209285645746e-02, 9.535298667798e-01,
			2.477804929697, 0.580483831447, -1.846729028753,
			4.664512974453e-09, -8.179282714095e-09, 2.285770803893e-10,
			-3.576278686523e-06, 5.448237061501e-06, 270.15625, -30.84375,
			-5.587935447693e-08, 6.332993507385e-08, -4.656612873077e-09),
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

	return $path;
}

/* === Test: full query (all types) === */
function test_full_query(string $dbPath): void
{
	echo "=== test_php_api: full query ===\n";

	$r = agnss_query($dbPath);

	assert_test($r['dataset']['id'] === 1, 'dataset id');
	assert_test($r['dataset']['timestamp'] === 1773064800, 'timestamp');
	assert_test($r['dataset']['gps_week'] === 2409, 'gps_week');
	assert_test($r['dataset']['source'] === 'test.rnx', 'source');
	assert_float($r['dataset']['location']['latitude'], 48.853, 'latitude');
	assert_float($r['dataset']['location']['longitude'], 2.3498, 'longitude');
	assert_test($r['dataset']['location']['altitude'] === 35, 'altitude');

	/* Ephemeris: 2 GPS + 1 QZSS */
	assert_test(count($r['ephemeris']) === 3, 'ephemeris count');
	assert_test($r['ephemeris'][0]['constellation'] === 'GPS', 'eph[0] constellation');
	assert_test($r['ephemeris'][0]['prn'] === 1, 'eph[0] prn');
	assert_test($r['ephemeris'][0]['iodc'] === 933, 'eph[0] iodc');
	assert_float($r['ephemeris'][0]['af0'], -2.578310668468e-05, 'eph[0] af0');
	assert_float($r['ephemeris'][0]['sqrt_a'], 5153.648925781250, 'eph[0] sqrt_a');
	assert_test($r['ephemeris'][1]['prn'] === 3, 'eph[1] prn');
	assert_test($r['ephemeris'][2]['constellation'] === 'QZSS', 'eph[2] constellation');
	assert_test($r['ephemeris'][2]['prn'] === 193, 'eph[2] prn');

	/* Almanac */
	assert_test(count($r['almanac']) === 1, 'almanac count');
	assert_test($r['almanac'][0]['prn'] === 1, 'alm prn');
	assert_test($r['almanac'][0]['ioda'] === 2, 'alm ioda');
	assert_float($r['almanac'][0]['e'], 5.48e-03, 'alm eccentricity');

	/* Ionosphere */
	assert_float($r['ionosphere']['alpha'][0], 1.118e-08, 'iono alpha0');
	assert_float($r['ionosphere']['beta'][2], -1.966e+05, 'iono beta2');

	/* UTC */
	assert_float($r['utc']['a0'], 1.863e-09, 'utc a0');
	assert_test($r['utc']['dt_ls'] === 18, 'utc dt_ls');
	assert_test($r['utc']['tot'] === 233472, 'utc tot');
}

/* === Test: type filtering === */
function test_type_filter(string $dbPath): void
{
	echo "=== test_php_api: type filtering ===\n";

	/* Only iono */
	$r = agnss_query($dbPath, ['iono']);
	assert_test(isset($r['ionosphere']), 'iono present');
	assert_test(!isset($r['ephemeris']), 'no ephemeris');
	assert_test(!isset($r['almanac']), 'no almanac');
	assert_test(!isset($r['utc']), 'no utc');

	/* Only ephe + utc */
	$r = agnss_query($dbPath, ['ephe', 'utc']);
	assert_test(isset($r['ephemeris']), 'ephemeris present');
	assert_test(isset($r['utc']), 'utc present');
	assert_test(!isset($r['ionosphere']), 'no iono');
	assert_test(!isset($r['almanac']), 'no almanac');
}

/* === Test: PRN filtering === */
function test_prn_filter(string $dbPath): void
{
	echo "=== test_php_api: PRN filtering ===\n";

	$r = agnss_query($dbPath, ['ephe'], [1]);
	assert_test(count($r['ephemeris']) === 1, 'ephe: 1 result');
	assert_test($r['ephemeris'][0]['prn'] === 1, 'ephe: PRN 1');

	$r = agnss_query($dbPath, ['ephe'], [1, 3]);
	assert_test(count($r['ephemeris']) === 2, 'ephe: 2 GPS results');

	$r = agnss_query($dbPath, ['alm'], [99]);
	assert_test(count($r['almanac']) === 0, 'alm: no match for PRN 99');
}

/* === Test: constellation filtering === */
function test_constellation_filter(string $dbPath): void
{
	echo "=== test_php_api: constellation filtering ===\n";

	$r = agnss_query($dbPath, ['ephe'], [], 'GPS');
	assert_test(count($r['ephemeris']) === 2, 'GPS only: 2 results');
	assert_test($r['ephemeris'][0]['constellation'] === 'GPS', 'GPS constellation');

	$r = agnss_query($dbPath, ['ephe'], [], 'QZSS');
	assert_test(count($r['ephemeris']) === 1, 'QZSS only: 1 result');
	assert_test($r['ephemeris'][0]['prn'] === 193, 'QZSS PRN 193');
}

/* === Test: missing database === */
function test_missing_db(): void
{
	echo "=== test_php_api: missing database ===\n";

	$caught = false;
	try {
		agnss_query('/tmp/nonexistent_db_' . getmypid() . '.sqlite');
	} catch (RuntimeException $e) {
		$caught = str_contains($e->getMessage(), 'Database not found');
	}
	assert_test($caught, 'missing db throws RuntimeException');
}

/* === Test: JSON round-trip === */
function test_json_roundtrip(string $dbPath): void
{
	echo "=== test_php_api: JSON round-trip ===\n";

	$r = agnss_query($dbPath);
	$json = json_encode($r, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
	assert_test(is_string($json), 'json_encode succeeds');
	/** @var non-empty-string $json */

	$decoded = json_decode((string)$json, true);
	assert_test(is_array($decoded), 'json_decode succeeds');
	assert_test($decoded['dataset']['gps_week'] === 2409, 'round-trip gps_week');
	assert_float($decoded['ephemeris'][0]['af0'], -2.578310668468e-05, 'round-trip af0');
	assert_float($decoded['ionosphere']['alpha'][0], 1.118e-08, 'round-trip iono alpha0');
}

/* === Test: help response === */
function test_help(): void
{
	echo "=== test_php_api: help response ===\n";

	$h = agnss_help();
	assert_test(count($h) > 0, 'help returns non-empty array');
	assert_test($h['name'] === 'rinex_dl A-GNSS REST API', 'help: name');
	assert_test(isset($h['parameters']), 'help: has parameters');
	assert_test(isset($h['parameters']['types']), 'help: has types param');
	assert_test(isset($h['parameters']['prn']), 'help: has prn param');
	assert_test(isset($h['parameters']['constellation']), 'help: has constellation param');
	assert_test(isset($h['parameters']['dataset']), 'help: has dataset param');
	assert_test(isset($h['examples']), 'help: has examples');
	assert_test(isset($h['parameters']['types']['values']['ephe']), 'help: types lists ephe');
	assert_test(isset($h['parameters']['types']['values']['iono']), 'help: types lists iono');

	$json = json_encode($h, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
	assert_test(is_string($json), 'help: JSON-encodable');
}

/* Run all tests */
$dbPath = create_test_db();

test_help();
test_full_query($dbPath);
test_type_filter($dbPath);
test_prn_filter($dbPath);
test_constellation_filter($dbPath);
test_missing_db();
test_json_roundtrip($dbPath);

unlink($dbPath);

printf("\ntest_php_api: %d/%d passed\n", $tests_passed, $tests_run);
/** @phpstan-ignore smaller.alwaysFalse (globals mutated in test functions) */
exit($tests_passed < $tests_run ? 1 : 0);
