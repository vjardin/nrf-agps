<?php
/*
 * index.php - A-GNSS REST API entry point
 *
 * Serves GPS assistance data from SQLite as JSON.
 *
 * GET /agnss?types=ephe,iono,utc,alm,loc&prn=1,3&constellation=GPS&dataset=1
 *
 * Deploy with PHP-FPM + nginx, or test with:
 *   AGNSS_DB_PATH=/path/to/db.sqlite php -S localhost:8080 -t php/
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

declare(strict_types=1);

require_once __DIR__ . '/agnss.php';

header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');

$dbPath = getenv('AGNSS_DB_PATH') ?: (__DIR__ . '/../agnss.db');

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
