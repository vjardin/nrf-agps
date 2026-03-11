<?php
/*
 * test_nrf_cloud.php - nRF Cloud binary encoder unit tests
 *
 * Tests nrf_cloud.php encoding functions against known struct sizes
 * and GPS ICD conversion values.
 * Run: php tests/test_nrf_cloud.php
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

declare(strict_types=1);

require_once __DIR__ . '/../php/agnss.php';
require_once __DIR__ . '/../php/nrf_cloud.php';

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

/* Create and populate a temp database (same schema as test_php_api.php) */
function create_test_db(): string
{
	$path = tempnam(sys_get_temp_dir(), 'test_nrf_cloud_');
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

/**
 * Unpack a uint16 little-endian from binary string at offset.
 */
function u16(string $data, int $off): int
{
	/** @var array{1: int} $v */
	$v = unpack('v', $data, $off);
	return $v[1];
}

/**
 * Unpack a uint32 little-endian from binary string at offset.
 */
function u32(string $data, int $off): int
{
	/** @var array{1: int} $v */
	$v = unpack('V', $data, $off);
	return $v[1];
}

/* === Test: ephemeris struct sizes === */
function test_ephemeris_struct(string $dbPath): void
{
	echo "=== test_nrf_cloud: ephemeris struct ===\n";

	$db = agnss_open($dbPath);
	$gps_rows = agnss_query_ephemeris($db, 1, [], 'GPS');
	$qzss_rows = agnss_query_ephemeris($db, 1, [], 'QZSS');

	/* Single record: 62 bytes */
	$one = nrf_cloud_pack_one_ephemeris($gps_rows[0]);
	assert_test(strlen($one) === 62, 'ephemeris: 62 bytes per SV');

	/* Group: 3-byte header + N * 62 */
	$group = nrf_cloud_encode_ephemeris($gps_rows);
	assert_test(strlen($group) === 3 + 2 * 62, 'ephemeris: header + 2 x 62');

	/* Header fields */
	assert_test(ord($group[0]) === NRF_CLOUD_AGNSS_EPHEMERIS, 'ephemeris: type ID = 2');
	assert_test(u16($group, 1) === 2, 'ephemeris: count = 2');

	/* First SV: PRN=1, health=0 */
	assert_test(ord($group[3]) === 1, 'ephemeris: SV[0] PRN = 1');
	assert_test(ord($group[4]) === 0, 'ephemeris: SV[0] health = 0');

	/* IODC field (uint16 at offset 5 within first record) */
	assert_test(u16($group, 5) === 933, 'ephemeris: SV[0] iodc = 933');

	/* TOC = 396000/16 = 24750 */
	assert_test(u16($group, 7) === 24750, 'ephemeris: SV[0] toc = 24750');

	/* Second SV: PRN=3 at offset 3+62 */
	assert_test(ord($group[65]) === 3, 'ephemeris: SV[1] PRN = 3');

	/* QZSS ephemeris: type 12 */
	$qzss_group = nrf_cloud_encode_qzss_ephemeris($qzss_rows);
	assert_test(strlen($qzss_group) === 3 + 62, 'QZSS ephemeris: header + 62');
	assert_test(ord($qzss_group[0]) === NRF_CLOUD_AGNSS_QZSS_EPHEMERIS,
		'QZSS ephemeris: type ID = 12');
	assert_test(ord($qzss_group[3]) === 193, 'QZSS ephemeris: PRN = 193');

	/* Empty input */
	assert_test(nrf_cloud_encode_ephemeris([]) === '', 'ephemeris: empty → empty');
}

/* === Test: almanac struct sizes === */
function test_almanac_struct(string $dbPath): void
{
	echo "=== test_nrf_cloud: almanac struct ===\n";

	$db = agnss_open($dbPath);
	$rows = agnss_query_almanac($db, 1, []);

	/* Single record: 31 bytes */
	$one = nrf_cloud_pack_one_almanac($rows[0]);
	assert_test(strlen($one) === 31, 'almanac: 31 bytes per SV');

	/* Group: 3-byte header + 31 */
	$group = nrf_cloud_encode_almanac($rows);
	assert_test(strlen($group) === 3 + 31, 'almanac: header + 31');

	/* Header */
	assert_test(ord($group[0]) === NRF_CLOUD_AGNSS_ALMANAC, 'almanac: type ID = 3');
	assert_test(u16($group, 1) === 1, 'almanac: count = 1');

	/* First SV: PRN=1 */
	assert_test(ord($group[3]) === 1, 'almanac: PRN = 1');

	/* WN = 2409 & 0xFF = 137 */
	assert_test(ord($group[4]) === (2409 & 0xFF), 'almanac: wn = 137');

	/* TOA = round(405504 / 4096) = 99 */
	assert_test(ord($group[5]) === 99, 'almanac: toa = 99');

	/* IODA = 2 */
	assert_test(ord($group[6]) === 2, 'almanac: ioda = 2');

	/* Health = 0 (at offset 3+4+2+2+2 = 13) */
	assert_test(ord($group[13]) === 0, 'almanac: health = 0');

	/* QZSS almanac: type 11 */
	$qzss = nrf_cloud_encode_qzss_almanac($rows);
	assert_test(ord($qzss[0]) === NRF_CLOUD_AGNSS_QZSS_ALMANAC,
		'QZSS almanac: type ID = 11');

	/* Empty input */
	assert_test(nrf_cloud_encode_almanac([]) === '', 'almanac: empty → empty');
}

/* === Test: Klobuchar ionospheric correction === */
function test_klobuchar(): void
{
	echo "=== test_nrf_cloud: Klobuchar ===\n";

	$iono = [
		'alpha0' => 1.118e-08, 'alpha1' => 0.0,
		'alpha2' => -5.961e-08, 'alpha3' => 0.0,
		'beta0' => 9.011e+04, 'beta1' => 0.0,
		'beta2' => -1.966e+05, 'beta3' => 0.0,
	];

	$data = nrf_cloud_encode_klobuchar($iono);

	/* 3-byte header + 8 data bytes */
	assert_test(strlen($data) === 3 + 8, 'klobuchar: 11 bytes total');
	assert_test(ord($data[0]) === NRF_CLOUD_AGNSS_KLOBUCHAR, 'klobuchar: type ID = 4');
	assert_test(u16($data, 1) === 1, 'klobuchar: count = 1');

	/* Verify alpha0 conversion: 1.118e-08 / 2^-30 = round(12.008...) = 12 */
	$a0 = ord($data[3]);
	assert_test($a0 === 12, 'klobuchar: alpha0 = 12');

	/* alpha1 = 0.0 → 0 */
	assert_test(ord($data[4]) === 0, 'klobuchar: alpha1 = 0');

	/* beta1 = 0.0 → 0 */
	assert_test(ord($data[8]) === 0, 'klobuchar: beta1 = 0');
}

/* === Test: UTC parameters === */
function test_utc(): void
{
	echo "=== test_nrf_cloud: UTC ===\n";

	$utc = [
		'a0' => 1.863e-09, 'a1' => 4.441e-15,
		'tot' => 233472, 'wnt' => 2409, 'dt_ls' => 18,
	];

	$data = nrf_cloud_encode_utc($utc);

	/* 3-byte header + 14 data bytes */
	assert_test(strlen($data) === 3 + 14, 'utc: 17 bytes total');
	assert_test(ord($data[0]) === NRF_CLOUD_AGNSS_UTC, 'utc: type ID = 1');
	assert_test(u16($data, 1) === 1, 'utc: count = 1');

	/* tot = round(233472 / 4096) = 57 — at byte offset 3+8=11 */
	assert_test(ord($data[11]) === 57, 'utc: tot = 57');

	/* wn_t = 2409 & 0xFF = 137 */
	assert_test(ord($data[12]) === (2409 & 0xFF), 'utc: wn_t = 137');

	/* dt_ls = 18 (signed int8) */
	assert_test(ord($data[13]) === 18, 'utc: dt_ls = 18');
}

/* === Test: system time === */
function test_system_time(): void
{
	echo "=== test_nrf_cloud: system time ===\n";

	$timestamp = 1773064800;
	$data = nrf_cloud_encode_system_time($timestamp);

	/* 3-byte header + 12 data bytes */
	assert_test(strlen($data) === 3 + 12, 'system_time: 15 bytes total');
	assert_test(ord($data[0]) === NRF_CLOUD_AGNSS_SYSTEM_CLOCK, 'system_time: type ID = 7');
	assert_test(u16($data, 1) === 1, 'system_time: count = 1');

	/* Verify GPS time computation */
	$gps_time = $timestamp - GPS_EPOCH_UNIX + GPS_LEAP_SECONDS;
	$expected_day = (int)($gps_time / 86400);
	$expected_tod = $gps_time % 86400;

	assert_test(u16($data, 3) === $expected_day, 'system_time: date_day');
	assert_test(u32($data, 5) === $expected_tod, 'system_time: time_full_s');
	assert_test(u16($data, 9) === 0, 'system_time: frac_ms = 0');
	assert_test(u32($data, 11) === 0, 'system_time: sv_mask = 0');
}

/* === Test: location === */
function test_location(): void
{
	echo "=== test_nrf_cloud: location ===\n";

	$data = nrf_cloud_encode_location(48.853, 2.3498, 35);

	/* 3-byte header + 15 data bytes */
	assert_test(strlen($data) === 3 + 15, 'location: 18 bytes total');
	assert_test(ord($data[0]) === NRF_CLOUD_AGNSS_LOCATION, 'location: type ID = 8');
	assert_test(u16($data, 1) === 1, 'location: count = 1');

	/* Verify latitude encoding direction (positive → positive int) */
	$lat_raw = u32($data, 3);
	assert_test($lat_raw > 0 && $lat_raw < 0x80000000, 'location: lat positive');

	/* Verify longitude encoding direction (positive → positive int) */
	$lon_raw = u32($data, 7);
	assert_test($lon_raw > 0 && $lon_raw < 0x80000000, 'location: lon positive');

	/* Altitude = 35 */
	assert_test(u16($data, 11) === 35, 'location: altitude = 35');

	/* Uncertainty fields */
	assert_test(ord($data[13]) === 127, 'location: unc_semimajor = 127');
	assert_test(ord($data[14]) === 127, 'location: unc_semiminor = 127');
	assert_test(ord($data[15]) === 0, 'location: orientation = 0');
	assert_test(ord($data[16]) === 255, 'location: unc_altitude = 255');
	assert_test(ord($data[17]) === 68, 'location: confidence = 68');
}

/* === Test: integrity === */
function test_integrity(): void
{
	echo "=== test_nrf_cloud: integrity ===\n";

	$data = nrf_cloud_encode_integrity();

	/* 3-byte header + 4 data bytes */
	assert_test(strlen($data) === 3 + 4, 'integrity: 7 bytes total');
	assert_test(ord($data[0]) === NRF_CLOUD_AGNSS_INTEGRITY, 'integrity: type ID = 9');
	assert_test(u16($data, 1) === 1, 'integrity: count = 1');
	assert_test(u32($data, 3) === 0xFFFFFFFF, 'integrity: all healthy');
}

/* === Test: full binary response === */
function test_full_response(string $dbPath): void
{
	echo "=== test_nrf_cloud: full response ===\n";

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

	/* First byte: schema version */
	assert_test(ord($payload[0]) === NRF_CLOUD_AGNSS_SCHEMA_VERSION,
		'response: schema version = 1');

	/* Walk the payload, verify element group structure */
	$pos = 1;
	$found_types = [];

	while ($pos < strlen($payload)) {
		$type_id = ord($payload[$pos]);
		$count = u16($payload, $pos + 1);
		$pos += 3;

		$found_types[] = $type_id;

		/* Compute expected data size based on type */
		switch ($type_id) {
		case NRF_CLOUD_AGNSS_UTC:
			$pos += $count * 14;
			break;
		case NRF_CLOUD_AGNSS_EPHEMERIS:
		case NRF_CLOUD_AGNSS_QZSS_EPHEMERIS:
			$pos += $count * 62;
			break;
		case NRF_CLOUD_AGNSS_ALMANAC:
		case NRF_CLOUD_AGNSS_QZSS_ALMANAC:
			$pos += $count * 31;
			break;
		case NRF_CLOUD_AGNSS_KLOBUCHAR:
			$pos += $count * 8;
			break;
		case NRF_CLOUD_AGNSS_SYSTEM_CLOCK:
			$pos += $count * 12;
			break;
		case NRF_CLOUD_AGNSS_LOCATION:
			$pos += $count * 15;
			break;
		case NRF_CLOUD_AGNSS_INTEGRITY:
		case NRF_CLOUD_AGNSS_QZSS_INTEGRITY:
			$pos += $count * 4;
			break;
		default:
			fprintf(STDERR, "  Unknown type %d at pos %d\n", $type_id, $pos);
			$pos = strlen($payload); /* bail */
		}
	}

	/* Verify we consumed exactly the full payload */
	assert_test($pos === strlen($payload), 'response: fully consumed');

	/* Verify expected types are present */
	assert_test(in_array(NRF_CLOUD_AGNSS_UTC, $found_types), 'response: has UTC');
	assert_test(in_array(NRF_CLOUD_AGNSS_EPHEMERIS, $found_types), 'response: has ephemeris');
	assert_test(in_array(NRF_CLOUD_AGNSS_ALMANAC, $found_types), 'response: has almanac');
	assert_test(in_array(NRF_CLOUD_AGNSS_KLOBUCHAR, $found_types), 'response: has klobuchar');
	assert_test(in_array(NRF_CLOUD_AGNSS_SYSTEM_CLOCK, $found_types), 'response: has sys_clock');
	assert_test(in_array(NRF_CLOUD_AGNSS_LOCATION, $found_types), 'response: has location');
	assert_test(in_array(NRF_CLOUD_AGNSS_INTEGRITY, $found_types), 'response: has integrity');
	assert_test(in_array(NRF_CLOUD_AGNSS_QZSS_EPHEMERIS, $found_types),
		'response: has QZSS ephemeris');
}

/* === Test: GPS ICD conversion spot checks === */
function test_icd_conversions(string $dbPath): void
{
	echo "=== test_nrf_cloud: GPS ICD conversions ===\n";

	$db = agnss_open($dbPath);
	$rows = agnss_query_ephemeris($db, 1, [1], 'GPS');
	$eph = nrf_cloud_pack_one_ephemeris($rows[0]);

	/* PRN=1 */
	assert_test(ord($eph[0]) === 1, 'icd: sv_id = 1');

	/* health=0 */
	assert_test(ord($eph[1]) === 0, 'icd: health = 0');

	/* iodc=933 (uint16 LE at offset 2) */
	assert_test(u16($eph, 2) === 933, 'icd: iodc = 933');

	/* toc = 396000/16 = 24750 (uint16 LE at offset 4) */
	assert_test(u16($eph, 4) === 24750, 'icd: toc = 24750');

	/* af2 = 0.0 / 2^-55 = 0 (int8 at offset 6) */
	assert_test(ord($eph[6]) === 0, 'icd: af2 = 0');

	/* toe = 396000/16 = 24750 (uint16 LE at offset 16) */
	assert_test(u16($eph, 16) === 24750, 'icd: toe = 24750');

	/* ura=0, fit_int=0 (bytes 14,15) */
	assert_test(ord($eph[14]) === 0, 'icd: ura = 0');
	assert_test(ord($eph[15]) === 0, 'icd: fit_int = 0');

	/* Verify e > 0 for GPS (uint32 at offset 32) */
	$e_raw = u32($eph, 32);
	assert_test($e_raw > 0, 'icd: eccentricity > 0');

	/* Almanac conversion checks */
	$alm_rows = agnss_query_almanac($db, 1, [1]);
	$alm = nrf_cloud_pack_one_almanac($alm_rows[0]);

	/* PRN=1 */
	assert_test(ord($alm[0]) === 1, 'icd alm: sv_id = 1');

	/* e = round(5.48e-03 / 2^-21) = round(11492.39...) = 11492 */
	$e_alm = u16($alm, 4);
	assert_test($e_alm === 11492, 'icd alm: e = 11492');
}

/* === Test: to_unsigned helper === */
function test_to_unsigned(): void
{
	echo "=== test_nrf_cloud: to_unsigned ===\n";

	assert_test(to_unsigned(0, 8) === 0, 'to_unsigned: 0 → 0');
	assert_test(to_unsigned(-1, 8) === 255, 'to_unsigned: -1/8 → 255');
	assert_test(to_unsigned(-1, 16) === 65535, 'to_unsigned: -1/16 → 65535');
	assert_test(to_unsigned(-1, 32) === 4294967295, 'to_unsigned: -1/32 → 4294967295');
	assert_test(to_unsigned(127, 8) === 127, 'to_unsigned: 127/8 → 127');
	assert_test(to_unsigned(-128, 8) === 128, 'to_unsigned: -128/8 → 128');
}

/* === Test: missing database === */
function test_missing_db(): void
{
	echo "=== test_nrf_cloud: missing database ===\n";

	$caught = false;
	try {
		nrf_cloud_build_response(
			'/tmp/nonexistent_db_' . getmypid() . '.sqlite',
			[NRF_CLOUD_AGNSS_EPHEMERIS]
		);
	} catch (RuntimeException $e) {
		$caught = str_contains($e->getMessage(), 'Database not found');
	}
	assert_test($caught, 'missing db throws RuntimeException');
}

/* Run all tests */
$dbPath = create_test_db();

test_to_unsigned();
test_ephemeris_struct($dbPath);
test_almanac_struct($dbPath);
test_klobuchar();
test_utc();
test_system_time();
test_location();
test_integrity();
test_icd_conversions($dbPath);
test_full_response($dbPath);
test_missing_db();

unlink($dbPath);

printf("\ntest_nrf_cloud: %d/%d passed\n", $tests_passed, $tests_run);
/** @phpstan-ignore smaller.alwaysFalse (globals mutated in test functions) */
exit($tests_passed < $tests_run ? 1 : 0);
