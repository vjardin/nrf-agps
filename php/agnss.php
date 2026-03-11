<?php
/*
 * agnss.php - A-GNSS data query functions
 *
 * Core logic for the REST API. Separated from index.php for testability.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

declare(strict_types=1);

const AGNSS_API_VERSION = 1;

/**
 * Return API help document.
 *
 * @return array<string, mixed>
 */
function agnss_help(): array
{
	return [
		'name'        => 'rinex_dl A-GNSS REST API',
		'version'     => AGNSS_API_VERSION,
		'description' => 'Serves GPS/QZSS assistance data from RINEX broadcast ephemeris.',
		'usage'       => 'GET /?types=ephe,alm,iono,utc,loc[&prn=1,3][&constellation=GPS][&dataset=1]',
		'parameters'  => [
			'types' => [
				'description' => 'Comma-separated data types to return',
				'values'      => [
					'ephe' => 'Satellite ephemerides (Keplerian orbital parameters)',
					'alm'  => 'Almanac entries (SEM/YUMA or derived from ephemeris)',
					'iono' => 'Klobuchar ionospheric correction (alpha/beta)',
					'utc'  => 'UTC parameters (A0, A1, leap seconds)',
					'loc'  => 'Reference location (latitude, longitude, altitude)',
				],
				'default' => 'all',
				'required' => true,
			],
			'prn' => [
				'description' => 'Comma-separated PRN numbers to filter',
				'example'     => '1,3,7,14',
				'default'     => 'all satellites',
			],
			'constellation' => [
				'description' => 'Filter by constellation',
				'values'      => ['GPS', 'QZSS'],
				'default'     => 'all',
			],
			'dataset' => [
				'description' => 'Metadata ID of a specific dataset',
				'default'     => 'latest',
			],
		],
		'examples' => [
			'All data'             => '/?types=ephe,alm,iono,utc,loc',
			'Ephemeris + iono'     => '/?types=ephe,iono',
			'Single satellite'     => '/?types=ephe&prn=1',
			'QZSS only'           => '/?types=ephe&constellation=QZSS',
			'UTC + iono'           => '/?types=utc,iono',
			'Location only'        => '/?types=loc',
			'Historical dataset'   => '/?dataset=1&types=ephe',
		],
		'source' => 'https://github.com/vjardin/nrf-agps',
	];
}

/**
 * Open the SQLite database in read-only mode.
 *
 * @throws RuntimeException if file missing or connection fails
 */
function agnss_open(string $dbPath): PDO
{
	if (!file_exists($dbPath)) {
		throw new RuntimeException('Database not found: ' . $dbPath);
	}

	return new PDO('sqlite:' . $dbPath, null, null, [
		PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
		PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
	]);
}

/**
 * Query A-GNSS data from the database.
 *
 * @param string              $dbPath        Path to SQLite database
 * @param list<string>|null   $types         Data types (ephe,alm,iono,utc,loc)
 * @param list<int>           $prns          Filter by PRN numbers (empty = all)
 * @param string|null         $constellation Filter: 'GPS', 'QZSS', or null
 * @param int|null            $datasetId     Metadata ID (null = latest)
 *
 * @return array<string, mixed> Response data
 * @throws RuntimeException on database errors
 */
function agnss_query(
	string $dbPath,
	?array $types = null,
	array $prns = [],
	?string $constellation = null,
	?int $datasetId = null
): array {
	/** @var list<string> $allTypes */
	$allTypes = ['ephe', 'alm', 'iono', 'utc', 'loc'];
	if ($types === null) {
		$types = $allTypes;
	}
	$types = array_map('trim', $types);

	$db = agnss_open($dbPath);

	$meta = agnss_query_metadata($db, $datasetId);
	if ($meta === null) {
		throw new RuntimeException('No dataset found');
	}

	$metaId = (int)$meta['id'];

	/** @var array<string, mixed> $response */
	$response = [
		'api_version' => AGNSS_API_VERSION,
		'dataset' => [
			'id'         => $metaId,
			'timestamp'  => (int)$meta['timestamp'],
			'gps_week'   => (int)$meta['gps_week'],
			'source'     => $meta['source_name'],
			'created_at' => $meta['created_at'],
		],
	];

	if (in_array('loc', $types) && $meta['ref_latitude'] !== null) {
		$response['dataset']['location'] = [
			'latitude'  => (float)$meta['ref_latitude'],
			'longitude' => (float)$meta['ref_longitude'],
			'altitude'  => (int)$meta['ref_altitude'],
		];
	}

	if (in_array('ephe', $types)) {
		$response['ephemeris'] = agnss_query_ephemeris(
			$db, $metaId, $prns, $constellation
		);
	}

	if (in_array('alm', $types)) {
		$response['almanac'] = agnss_query_almanac($db, $metaId, $prns);
	}

	if (in_array('iono', $types)) {
		$iono = agnss_query_ionosphere($db, $metaId);
		if ($iono !== null) {
			$response['ionosphere'] = $iono;
		}
	}

	if (in_array('utc', $types)) {
		$utc = agnss_query_utc($db, $metaId);
		if ($utc !== null) {
			$response['utc'] = $utc;
		}
	}

	return $response;
}

/**
 * @internal
 * @return array<string, mixed>|null
 */
function agnss_query_metadata(PDO $db, ?int $datasetId): ?array
{
	if ($datasetId !== null) {
		$stmt = $db->prepare(
			'SELECT * FROM metadata WHERE id = ?'
		);
		$stmt->execute([$datasetId]);
	} else {
		$stmt = $db->prepare(
			'SELECT * FROM metadata ORDER BY id DESC LIMIT 1'
		);
		$stmt->execute();
	}

	$row = $stmt->fetch();
	return is_array($row) ? $row : null;
}

/**
 * @internal
 * @param list<int> $prns
 * @return list<array<string, mixed>>
 */
function agnss_query_ephemeris(
	PDO $db, int $metaId, array $prns, ?string $constellation
): array {
	$sql = 'SELECT * FROM ephemeris WHERE metadata_id = ?';
	/** @var list<int|string> $params */
	$params = [$metaId];

	if ($constellation !== null) {
		$sql .= ' AND constellation = ?';
		$params[] = strtoupper($constellation);
	}

	if (!empty($prns)) {
		$ph = implode(',', array_fill(0, count($prns), '?'));
		$sql .= " AND prn IN ($ph)";
		$params = array_merge($params, $prns);
	}

	$sql .= ' ORDER BY constellation, prn';
	$stmt = $db->prepare($sql);
	$stmt->execute($params);

	return array_values(array_map(function (array $row): array {
		return [
			'constellation' => $row['constellation'],
			'prn'       => (int)$row['prn'],
			'health'    => (int)$row['health'],
			'iodc'      => (int)$row['iodc'],
			'iode'      => (int)$row['iode'],
			'week'      => (int)$row['week'],
			'toe'       => (int)$row['toe'],
			'toc'       => (int)$row['toc'],
			'af0'       => (float)$row['af0'],
			'af1'       => (float)$row['af1'],
			'af2'       => (float)$row['af2'],
			'sqrt_a'    => (float)$row['sqrt_a'],
			'e'         => (float)$row['e'],
			'i0'        => (float)$row['i0'],
			'omega0'    => (float)$row['omega0'],
			'omega'     => (float)$row['omega'],
			'm0'        => (float)$row['m0'],
			'delta_n'   => (float)$row['delta_n'],
			'omega_dot' => (float)$row['omega_dot'],
			'idot'      => (float)$row['idot'],
			'cuc'       => (float)$row['cuc'],
			'cus'       => (float)$row['cus'],
			'crc'       => (float)$row['crc'],
			'crs'       => (float)$row['crs'],
			'cic'       => (float)$row['cic'],
			'cis'       => (float)$row['cis'],
			'tgd'       => (float)$row['tgd'],
		];
	}, $stmt->fetchAll()));
}

/**
 * @internal
 * @param list<int> $prns
 * @return list<array<string, mixed>>
 */
function agnss_query_almanac(PDO $db, int $metaId, array $prns): array
{
	$sql = 'SELECT * FROM almanac WHERE metadata_id = ?';
	/** @var list<int> $params */
	$params = [$metaId];

	if (!empty($prns)) {
		$ph = implode(',', array_fill(0, count($prns), '?'));
		$sql .= " AND prn IN ($ph)";
		$params = array_merge($params, $prns);
	}

	$sql .= ' ORDER BY prn';
	$stmt = $db->prepare($sql);
	$stmt->execute($params);

	return array_values(array_map(function (array $row): array {
		return [
			'prn'       => (int)$row['prn'],
			'health'    => (int)$row['health'],
			'ioda'      => (int)$row['ioda'],
			'week'      => (int)$row['week'],
			'toa'       => (int)$row['toa'],
			'e'         => (float)$row['e'],
			'delta_i'   => (float)$row['delta_i'],
			'omega_dot' => (float)$row['omega_dot'],
			'sqrt_a'    => (float)$row['sqrt_a'],
			'omega0'    => (float)$row['omega0'],
			'omega'     => (float)$row['omega'],
			'm0'        => (float)$row['m0'],
			'af0'       => (float)$row['af0'],
			'af1'       => (float)$row['af1'],
		];
	}, $stmt->fetchAll()));
}

/**
 * @internal
 * @return array{alpha: list<float>, beta: list<float>}|null
 */
function agnss_query_ionosphere(PDO $db, int $metaId): ?array
{
	$stmt = $db->prepare(
		'SELECT * FROM ionosphere WHERE metadata_id = ?'
	);
	$stmt->execute([$metaId]);
	$row = $stmt->fetch();

	if (!is_array($row)) {
		return null;
	}

	return [
		'alpha' => [
			(float)$row['alpha0'], (float)$row['alpha1'],
			(float)$row['alpha2'], (float)$row['alpha3'],
		],
		'beta' => [
			(float)$row['beta0'], (float)$row['beta1'],
			(float)$row['beta2'], (float)$row['beta3'],
		],
	];
}

/**
 * @internal
 * @return array{a0: float, a1: float, tot: int, wnt: int, dt_ls: int}|null
 */
function agnss_query_utc(PDO $db, int $metaId): ?array
{
	$stmt = $db->prepare(
		'SELECT * FROM utc_params WHERE metadata_id = ?'
	);
	$stmt->execute([$metaId]);
	$row = $stmt->fetch();

	if (!is_array($row)) {
		return null;
	}

	return [
		'a0'    => (float)$row['a0'],
		'a1'    => (float)$row['a1'],
		'tot'   => (int)$row['tot'],
		'wnt'   => (int)$row['wnt'],
		'dt_ls' => (int)$row['dt_ls'],
	];
}
