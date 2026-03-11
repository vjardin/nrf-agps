<?php
/*
 * nrf_cloud.php - nRF Cloud A-GNSS binary encoder
 *
 * Builds binary responses compatible with nRF Cloud schema v1,
 * as parsed by nrf_cloud_agnss_process() in the nRF Connect SDK.
 *
 * Binary format: [0x01 schema] [type(1) count(2 LE) packed_data...]...
 * All integers are little-endian, structs are __packed.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

declare(strict_types=1);

const NRF_CLOUD_AGNSS_SCHEMA_VERSION = 1;

/* nRF Cloud A-GNSS type IDs (schema v1) */
const NRF_CLOUD_AGNSS_UTC            = 1;
const NRF_CLOUD_AGNSS_EPHEMERIS      = 2;
const NRF_CLOUD_AGNSS_ALMANAC        = 3;
const NRF_CLOUD_AGNSS_KLOBUCHAR      = 4;
const NRF_CLOUD_AGNSS_NEQUICK        = 5;
const NRF_CLOUD_AGNSS_TOW            = 6;
const NRF_CLOUD_AGNSS_SYSTEM_CLOCK   = 7;
const NRF_CLOUD_AGNSS_LOCATION       = 8;
const NRF_CLOUD_AGNSS_INTEGRITY      = 9;
const NRF_CLOUD_AGNSS_QZSS_ALMANAC   = 11;
const NRF_CLOUD_AGNSS_QZSS_EPHEMERIS = 12;
const NRF_CLOUD_AGNSS_QZSS_INTEGRITY = 13;

/* GPS epoch: Jan 6 1980 00:00:00 UTC */
const GPS_EPOCH_UNIX = 315964800;
const GPS_LEAP_SECONDS = 18;

/**
 * Pack an element group header: type(1) + count(2 LE).
 */
function nrf_cloud_pack_header(int $type, int $count): string
{
	return pack('Cv', $type, $count);
}

/**
 * Convert a signed integer to unsigned for pack(), masking to $bits.
 */
function to_unsigned(int $val, int $bits): int
{
	return $val & ((1 << $bits) - 1);
}

/**
 * Encode GPS ephemeris (type 2) — 62 bytes per SV.
 *
 * Input: array of DB rows with double-precision values (radians for angles).
 * Output: binary element group.
 *
 * @param list<array<string, mixed>> $rows
 */
function nrf_cloud_encode_ephemeris(array $rows): string
{
	if (empty($rows)) {
		return '';
	}

	$data = nrf_cloud_pack_header(NRF_CLOUD_AGNSS_EPHEMERIS, count($rows));

	foreach ($rows as $r) {
		$data .= nrf_cloud_pack_one_ephemeris($r);
	}

	return $data;
}

/**
 * Encode QZSS ephemeris (type 12) — same struct as GPS.
 *
 * @param list<array<string, mixed>> $rows
 */
function nrf_cloud_encode_qzss_ephemeris(array $rows): string
{
	if (empty($rows)) {
		return '';
	}

	$data = nrf_cloud_pack_header(NRF_CLOUD_AGNSS_QZSS_EPHEMERIS, count($rows));

	foreach ($rows as $r) {
		$data .= nrf_cloud_pack_one_ephemeris($r);
	}

	return $data;
}

/**
 * Pack one ephemeris record (62 bytes).
 *
 * @param array<string, mixed> $r DB row
 */
function nrf_cloud_pack_one_ephemeris(array $r): string
{
	$pi = M_PI;

	/* Scale conversions: RINEX doubles → GPS ICD integers */
	$sv_id   = (int)$r['prn'];
	$health  = (int)$r['health'];
	$iodc    = (int)$r['iodc'];
	$toc     = (int)((int)$r['toc'] / 16);
	$af2     = (int)round((float)$r['af2'] / pow(2, -55));
	$af1     = (int)round((float)$r['af1'] / pow(2, -43));
	$af0     = (int)round((float)$r['af0'] / pow(2, -31));
	$tgd     = (int)round((float)$r['tgd'] / pow(2, -31));
	$ura     = 0;
	$fit_int = 0;
	$toe     = (int)((int)$r['toe'] / 16);
	$w       = (int)round((float)$r['omega'] / $pi * pow(2, 31));
	$delta_n = (int)round((float)$r['delta_n'] / $pi * pow(2, 43));
	$m0      = (int)round((float)$r['m0'] / $pi * pow(2, 31));
	$omega_dot = (int)round((float)$r['omega_dot'] / $pi * pow(2, 43));
	$e       = (int)round((float)$r['e'] / pow(2, -33));
	$idot    = (int)round((float)$r['idot'] / $pi * pow(2, 43));
	$sqrt_a  = (int)round((float)$r['sqrt_a'] / pow(2, -19));
	$i0      = (int)round((float)$r['i0'] / $pi * pow(2, 31));
	$omega0  = (int)round((float)$r['omega0'] / $pi * pow(2, 31));
	$crs     = (int)round((float)$r['crs'] / pow(2, -5));
	$cis     = (int)round((float)$r['cis'] / pow(2, -29));
	$cus     = (int)round((float)$r['cus'] / pow(2, -29));
	$crc     = (int)round((float)$r['crc'] / pow(2, -5));
	$cic     = (int)round((float)$r['cic'] / pow(2, -29));
	$cuc     = (int)round((float)$r['cuc'] / pow(2, -29));

	/* Pack: CCvvcvVcCCvVvVVVvVVVvvvvvv (62 bytes) */
	return pack('CCvv',
			$sv_id, $health, $iodc, $toc) .
		pack('c', $af2) .
		pack('v', to_unsigned($af1, 16)) .
		pack('V', to_unsigned($af0, 32)) .
		pack('c', $tgd) .
		pack('CC', $ura, $fit_int) .
		pack('v', $toe) .
		pack('V', to_unsigned($w, 32)) .
		pack('v', to_unsigned($delta_n, 16)) .
		pack('V', to_unsigned($m0, 32)) .
		pack('V', to_unsigned($omega_dot, 32)) .
		pack('V', $e) .
		pack('v', to_unsigned($idot, 16)) .
		pack('V', $sqrt_a) .
		pack('V', to_unsigned($i0, 32)) .
		pack('V', to_unsigned($omega0, 32)) .
		pack('v', to_unsigned($crs, 16)) .
		pack('v', to_unsigned($cis, 16)) .
		pack('v', to_unsigned($cus, 16)) .
		pack('v', to_unsigned($crc, 16)) .
		pack('v', to_unsigned($cic, 16)) .
		pack('v', to_unsigned($cuc, 16));
}

/**
 * Encode GPS almanac (type 3) — 31 bytes per SV.
 *
 * Input: DB rows with semi-circle angles (as stored by almanac parser).
 *
 * @param list<array<string, mixed>> $rows
 */
function nrf_cloud_encode_almanac(array $rows): string
{
	if (empty($rows)) {
		return '';
	}

	$data = nrf_cloud_pack_header(NRF_CLOUD_AGNSS_ALMANAC, count($rows));

	foreach ($rows as $r) {
		$data .= nrf_cloud_pack_one_almanac($r);
	}

	return $data;
}

/**
 * Encode QZSS almanac (type 11) — same struct as GPS.
 *
 * @param list<array<string, mixed>> $rows
 */
function nrf_cloud_encode_qzss_almanac(array $rows): string
{
	if (empty($rows)) {
		return '';
	}

	$data = nrf_cloud_pack_header(NRF_CLOUD_AGNSS_QZSS_ALMANAC, count($rows));

	foreach ($rows as $r) {
		$data .= nrf_cloud_pack_one_almanac($r);
	}

	return $data;
}

/**
 * Pack one almanac record (31 bytes).
 *
 * @param array<string, mixed> $r DB row (angles in semi-circles)
 */
function nrf_cloud_pack_one_almanac(array $r): string
{
	$sv_id     = (int)$r['prn'];
	$wn        = (int)$r['week'] & 0xFF;
	$toa       = (int)round((int)$r['toa'] / 4096.0);
	$ioda      = (int)$r['ioda'];
	$e         = (int)round((float)$r['e'] / pow(2, -21));
	$delta_i   = (int)round((float)$r['delta_i'] / pow(2, -19));
	$omega_dot = (int)round((float)$r['omega_dot'] / pow(2, -38));
	$sv_health = (int)$r['health'];
	$sqrt_a    = (int)round((float)$r['sqrt_a'] / pow(2, -11));
	$omega0    = (int)round((float)$r['omega0'] / pow(2, -23));
	$w         = (int)round((float)$r['omega'] / pow(2, -23));
	$m0        = (int)round((float)$r['m0'] / pow(2, -23));
	$af0       = (int)round((float)$r['af0'] / pow(2, -20));
	$af1       = (int)round((float)$r['af1'] / pow(2, -38));

	return pack('CCCC', $sv_id, $wn, $toa, $ioda) .
		pack('v', $e) .
		pack('v', to_unsigned($delta_i, 16)) .
		pack('v', to_unsigned($omega_dot, 16)) .
		pack('C', $sv_health) .
		pack('V', $sqrt_a) .
		pack('V', to_unsigned($omega0, 32)) .
		pack('V', to_unsigned($w, 32)) .
		pack('V', to_unsigned($m0, 32)) .
		pack('v', to_unsigned($af0, 16)) .
		pack('v', to_unsigned($af1, 16));
}

/**
 * Encode Klobuchar ionospheric correction (type 4) — 8 bytes.
 *
 * @param array<string, mixed> $iono Row from ionosphere table
 */
function nrf_cloud_encode_klobuchar(array $iono): string
{
	$a0 = (int)round((float)$iono['alpha0'] / pow(2, -30));
	$a1 = (int)round((float)$iono['alpha1'] / pow(2, -27));
	$a2 = (int)round((float)$iono['alpha2'] / pow(2, -24));
	$a3 = (int)round((float)$iono['alpha3'] / pow(2, -24));
	$b0 = (int)round((float)$iono['beta0'] / pow(2, 11));
	$b1 = (int)round((float)$iono['beta1'] / pow(2, 14));
	$b2 = (int)round((float)$iono['beta2'] / pow(2, 16));
	$b3 = (int)round((float)$iono['beta3'] / pow(2, 16));

	return nrf_cloud_pack_header(NRF_CLOUD_AGNSS_KLOBUCHAR, 1) .
		pack('cccccccc', $a0, $a1, $a2, $a3, $b0, $b1, $b2, $b3);
}

/**
 * Encode UTC parameters (type 1) — 14 bytes.
 *
 * @param array<string, mixed> $utc Row from utc_params table
 */
function nrf_cloud_encode_utc(array $utc): string
{
	$a1     = (int)round((float)$utc['a1'] / pow(2, -50));
	$a0     = (int)round((float)$utc['a0'] / pow(2, -30));
	$tot    = (int)round((int)$utc['tot'] / 4096.0);
	$wn_t   = (int)$utc['wnt'] & 0xFF;
	$dt_ls  = (int)$utc['dt_ls'];
	/* wn_lsf, dn, delta_tlsf not in our DB — use same as current */
	$wn_lsf     = $wn_t;
	$dn         = 0;
	$delta_tlsf = $dt_ls;

	return nrf_cloud_pack_header(NRF_CLOUD_AGNSS_UTC, 1) .
		pack('V', to_unsigned($a1, 32)) .
		pack('V', to_unsigned($a0, 32)) .
		pack('CCcCcc', $tot, $wn_t, $dt_ls, $wn_lsf, $dn, $delta_tlsf);
}

/**
 * Encode GPS system clock (type 7) — 12 bytes.
 * No TOW elements (type 6) since we lack TLM data.
 */
function nrf_cloud_encode_system_time(int $timestamp): string
{
	$gps_time  = $timestamp - GPS_EPOCH_UNIX + GPS_LEAP_SECONDS;
	$date_day  = (int)($gps_time / 86400);
	$time_of_day = $gps_time % 86400;
	$time_full_s  = (int)$time_of_day;
	$time_frac_ms = 0;
	$sv_mask      = 0;  /* no per-SV TOW data */

	return nrf_cloud_pack_header(NRF_CLOUD_AGNSS_SYSTEM_CLOCK, 1) .
		pack('vVvV', $date_day, $time_full_s, $time_frac_ms, $sv_mask);
}

/**
 * Encode location (type 8) — 15 bytes.
 */
function nrf_cloud_encode_location(float $lat, float $lon, int $alt): string
{
	/* Scale: lat → N where N <= (2^23/90) * lat < N+1 */
	$lat_enc = (int)floor($lat * (1 << 23) / 90.0);
	/* Scale: lon → N where N <= (2^24/360) * lon < N+1 */
	$lon_enc = (int)floor($lon * (1 << 24) / 360.0);

	$unc_semimajor = 127;  /* max uncertainty (~1800 km) */
	$unc_semiminor = 127;
	$orientation   = 0;
	$unc_altitude  = 255;  /* missing */
	$confidence    = 68;   /* ~1 sigma */

	return nrf_cloud_pack_header(NRF_CLOUD_AGNSS_LOCATION, 1) .
		pack('V', to_unsigned($lat_enc, 32)) .
		pack('V', to_unsigned($lon_enc, 32)) .
		pack('v', to_unsigned($alt, 16)) .
		pack('CCCCC', $unc_semimajor, $unc_semiminor,
			$orientation, $unc_altitude, $confidence);
}

/**
 * Encode integrity (type 9) — 4 bytes (all healthy).
 */
function nrf_cloud_encode_integrity(): string
{
	return nrf_cloud_pack_header(NRF_CLOUD_AGNSS_INTEGRITY, 1) .
		pack('V', 0xFFFFFFFF);
}

/**
 * Build a complete nRF Cloud binary A-GNSS response.
 *
 * @param string    $dbPath  Path to SQLite database
 * @param list<int> $types   Requested nRF Cloud type IDs
 * @return string Binary payload
 * @throws RuntimeException on database errors
 */
function nrf_cloud_build_response(string $dbPath, array $types): string
{
	$db = agnss_open($dbPath);

	$meta = agnss_query_metadata($db, null);
	if ($meta === null) {
		throw new RuntimeException('No dataset found');
	}

	$metaId = (int)$meta['id'];
	$binary = chr(NRF_CLOUD_AGNSS_SCHEMA_VERSION);

	foreach ($types as $type) {
		switch ($type) {
		case NRF_CLOUD_AGNSS_UTC:
			$utc = agnss_query_utc($db, $metaId);
			if ($utc !== null) {
				$binary .= nrf_cloud_encode_utc($utc);
			}
			break;

		case NRF_CLOUD_AGNSS_EPHEMERIS:
			$rows = agnss_query_ephemeris($db, $metaId, [], 'GPS');
			$binary .= nrf_cloud_encode_ephemeris($rows);
			break;

		case NRF_CLOUD_AGNSS_ALMANAC:
			$rows = agnss_query_almanac($db, $metaId, []);
			$binary .= nrf_cloud_encode_almanac($rows);
			break;

		case NRF_CLOUD_AGNSS_KLOBUCHAR:
			$iono = agnss_query_ionosphere($db, $metaId);
			if ($iono !== null) {
				$binary .= nrf_cloud_encode_klobuchar([
					'alpha0' => $iono['alpha'][0],
					'alpha1' => $iono['alpha'][1],
					'alpha2' => $iono['alpha'][2],
					'alpha3' => $iono['alpha'][3],
					'beta0'  => $iono['beta'][0],
					'beta1'  => $iono['beta'][1],
					'beta2'  => $iono['beta'][2],
					'beta3'  => $iono['beta'][3],
				]);
			}
			break;

		case NRF_CLOUD_AGNSS_TOW:
			/* No TLM data available — emit empty group */
			$binary .= nrf_cloud_pack_header(NRF_CLOUD_AGNSS_TOW, 0);
			break;

		case NRF_CLOUD_AGNSS_SYSTEM_CLOCK:
			$binary .= nrf_cloud_encode_system_time(
				(int)$meta['timestamp']
			);
			break;

		case NRF_CLOUD_AGNSS_LOCATION:
			if ($meta['ref_latitude'] !== null) {
				$binary .= nrf_cloud_encode_location(
					(float)$meta['ref_latitude'],
					(float)$meta['ref_longitude'],
					(int)$meta['ref_altitude']
				);
			}
			break;

		case NRF_CLOUD_AGNSS_INTEGRITY:
			$binary .= nrf_cloud_encode_integrity();
			break;

		case NRF_CLOUD_AGNSS_QZSS_EPHEMERIS:
			$rows = agnss_query_ephemeris($db, $metaId, [], 'QZSS');
			$binary .= nrf_cloud_encode_qzss_ephemeris($rows);
			break;

		case NRF_CLOUD_AGNSS_QZSS_ALMANAC:
			/* QZSS almanac not stored separately — skip */
			break;

		case NRF_CLOUD_AGNSS_QZSS_INTEGRITY:
			$binary .= nrf_cloud_pack_header(
				NRF_CLOUD_AGNSS_QZSS_INTEGRITY, 1
			);
			$binary .= pack('V', 0xFFFFFFFF);
			break;
		}
	}

	return $binary;
}

/**
 * Handle a POST /v1/location/agnss request.
 *
 * Parses the JSON body, builds the binary response, and handles
 * Range headers for fragmented delivery.
 */
function nrf_cloud_handle_request(string $dbPath): void
{
	$body = file_get_contents('php://input');
	if ($body === false || $body === '') {
		header('Content-Type: application/json');
		http_response_code(400);
		echo json_encode(['error' => 'Empty request body']);
		return;
	}

	$req = json_decode($body, true);
	if (!is_array($req) || !isset($req['types']) || !is_array($req['types'])) {
		header('Content-Type: application/json');
		http_response_code(400);
		echo json_encode(['error' => 'Missing or invalid "types" array']);
		return;
	}

	/** @var list<int> $types */
	$types = array_map('intval', $req['types']);

	try {
		$payload = nrf_cloud_build_response($dbPath, $types);
	} catch (RuntimeException $e) {
		header('Content-Type: application/json');
		http_response_code(404);
		echo json_encode(['error' => $e->getMessage()]);
		return;
	}

	$total = strlen($payload);

	/* Handle Range header for fragmented delivery */
	$range = $_SERVER['HTTP_RANGE'] ?? '';
	if (preg_match('/^bytes=(\d+)-(\d+)$/', $range, $m)) {
		$start = (int)$m[1];
		$end   = min((int)$m[2], $total - 1);

		if ($start >= $total) {
			http_response_code(416);
			header("Content-Range: bytes */$total");
			return;
		}

		header('Content-Type: application/octet-stream');
		header(sprintf('Content-Range: bytes %d-%d/%d',
			$start, $end, $total));
		http_response_code(206);
		echo substr($payload, $start, $end - $start + 1);
	} else {
		header('Content-Type: application/octet-stream');
		header('Content-Length: ' . $total);
		http_response_code(200);
		echo $payload;
	}
}
