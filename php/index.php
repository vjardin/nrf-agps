<?php
/*
 * index.php - A-GNSS REST API entry point
 *
 * Serves GPS assistance data from SQLite as JSON,
 * and nRF Cloud-compatible binary A-GNSS via POST.
 *
 * GET /                       -> API help
 * GET /?types=ephe,iono       -> query data (JSON)
 * POST /v1/location/agnss     -> nRF Cloud binary A-GNSS
 *
 * Deploy with PHP-FPM + nginx, or test with:
 *   AGNSS_DB_PATH=/path/to/db.sqlite php -S localhost:8080 php/index.php
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

declare(strict_types=1);

require_once __DIR__ . '/agnss.php';
require_once __DIR__ . '/nrf_cloud.php';

header('Access-Control-Allow-Origin: *');

$dbPath = getenv('AGNSS_DB_PATH') ?: (__DIR__ . '/../agnss.db');

/* nRF Cloud binary A-GNSS endpoint */
$requestPath = parse_url($_SERVER['REQUEST_URI'] ?? '', PHP_URL_PATH);
if ($_SERVER['REQUEST_METHOD'] === 'POST' && $requestPath === '/v1/location/agnss') {
	nrf_cloud_handle_request($dbPath);
	exit;
}

/* JSON REST API */
header('Content-Type: application/json');

/* No query parameters → show API help */
if (empty($_GET)) {
	echo json_encode(agnss_help(), JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
	exit;
}

$types = isset($_GET['types'])
	? explode(',', $_GET['types'])
	: null;

$prns = isset($_GET['prn'])
	? array_map('intval', explode(',', $_GET['prn']))
	: [];

$constellation = isset($_GET['constellation'])
	? $_GET['constellation']
	: null;

$datasetId = isset($_GET['dataset'])
	? (int)$_GET['dataset']
	: null;

try {
	$result = agnss_query($dbPath, $types, $prns, $constellation, $datasetId);
	echo json_encode($result, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
} catch (RuntimeException $e) {
	http_response_code($e->getMessage() === 'No dataset found' ? 404 : 500);
	echo json_encode(['error' => $e->getMessage()]);
}
