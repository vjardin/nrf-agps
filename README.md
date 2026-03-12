# rinex_dl — Offline A-GNSS data generator and SUPL 2.0 server for nRF9151

[![CI](https://github.com/vjardin/nrf-agps/actions/workflows/ci.yml/badge.svg)](https://github.com/vjardin/nrf-agps/actions/workflows/ci.yml)

Downloads daily GPS broadcast ephemeris from public RINEX sources and
provides multiple delivery paths for Assisted GNSS data to reduce
Time To First Fix (TTFF) on the nRF9151:

| Component        | Description                                            |
|------------------|--------------------------------------------------------|
| `rinex_dl`       | Downloads RINEX, generates C source or SQLite database |
| `supl_server`    | OMA SUPL 2.0 server with 3GPP LPP assistance data      |
| `php/`           | JSON REST API + nRF Cloud binary A-GNSS endpoint       |
| `tests/supl_client` | SUPL 2.0 diagnostic client with JSON export         |

## Build

### Dependencies

```sh
# Debian/Ubuntu — core (rinex_dl)
sudo apt install libcurl4-openssl-dev zlib1g-dev libsqlite3-dev \
  pkg-config meson ninja-build

# SUPL server and client (additional)
sudo apt install libevent-dev libssl-dev libcjson-dev
```

The SUPL components also require
[mouse07410/asn1c](https://github.com/mouse07410/asn1c) v1.4+
(the Debian/Ubuntu `asn1c` package is too old):

```sh
git clone https://github.com/mouse07410/asn1c.git
cd asn1c && test -f configure || autoreconf -iv
./configure && make && sudo make install
cd -
```

### Compiling

```sh
meson setup builddir
meson compile -C builddir
```

Meson builds all targets whose dependencies are satisfied. If SUPL
libraries (libevent, OpenSSL) are missing, `rinex_dl` is still built
but `supl_server` and `supl_client` are skipped.

ASN.1 codec sources are auto-generated into the build directory at
configure time when `asn1c` is installed. To regenerate explicitly:

```sh
ninja -C builddir regenerate-asn1
```

## Usage

```
./builddir/rinex_dl [options]

Options:
  -d YYYY-MM-DD  Date to download (default: yesterday)
  -l LAT,LON    Approximate reference location (default: Paris Notre Dame)
  -o PREFIX      Output file prefix (default: gps_assist_data)
  -s DB_PATH     Store to SQLite database (instead of C source)
  -u URL         Custom RINEX navigation file URL
  -f FILE        Parse local RINEX file instead of downloading
  -a MODE        Almanac source (see below)
  -h             Show this help
```

### Almanac modes (`-a`)

| Mode        | Description                                  |
|-------------|----------------------------------------------|
| `derived`   | (default) Derive almanac from ephemeris data |
| `sem`       | Download current SEM almanac from USCG NAVCEN|
| `sem:FILE`  | Parse a local SEM almanac file (.al3)        |
| `yuma:FILE` | Parse a local YUMA almanac file              |

When a parsed almanac is available (SEM/YUMA), it is preferred over
ephemeris-derived data during injection. The fallback to derived almanac
is per-PRN: if a PRN has no parsed almanac but has an ephemeris, the
derived almanac is used.

### Examples

```sh
# Download yesterday's broadcast ephemeris (default)
./builddir/rinex_dl

# Specific date
./builddir/rinex_dl -d 2026-03-08

# Parse a local RINEX file
./builddir/rinex_dl -f /path/to/BRDC00IGS_R_20260680000_01D_MN.rnx.gz

# Custom output path
./builddir/rinex_dl -o /path/to/zephyr_project/src/gps_assist_data

# Override default location (e.g. Berlin)
./builddir/rinex_dl -l 52.5200,13.4050

# Download SEM almanac from NAVCEN instead of deriving from ephemeris
./builddir/rinex_dl -a sem

# Use a local SEM almanac file
./builddir/rinex_dl -a sem:/path/to/current_sem.al3

# Use a local YUMA almanac file
./builddir/rinex_dl -a yuma:/path/to/almanac.yuma

# Store to SQLite database (instead of C source)
./builddir/rinex_dl -s /var/lib/agnss/rinex.db

# SQLite with SEM almanac
./builddir/rinex_dl -s /var/lib/agnss/rinex.db -a sem
```

The tool downloads the combined BRDC navigation file from
[BKG IGS](https://igs.bkg.bund.de/) (no authentication required),
parses GPS satellite records, and writes `gps_assist_data.c` or
populates a SQLite database (`-s`).

### Daily cron job

```
# /etc/cron.d/rinex-agps
# Run at 02:00 UTC daily — yesterday's BRDC file is complete by then
# C source output:
0 2 * * * user /path/to/builddir/rinex_dl -o /path/to/zephyr_project/src/gps_assist_data
# SQLite output (for REST API serving):
0 2 * * * user /path/to/builddir/rinex_dl -s /var/lib/agnss/rinex.db
```

## PHP REST API

The `php/` directory provides a JSON REST API that reads from the SQLite
database populated by `rinex_dl -s`.

### Prerequisites

```sh
# Debian/Ubuntu
sudo apt install php-cli php-sqlite3
```

### Running the server

First, populate the database, then start the PHP built-in server:

```sh
# 1. Fetch yesterday's ephemeris into a SQLite database
./builddir/rinex_dl -s agnss.db

# 2. Start the server (listens on port 8080)
AGNSS_DB_PATH=agnss.db php -S localhost:8080 php/index.php
```

The server runs in the foreground and logs requests to stderr.
Press Ctrl-C to stop.  The `AGNSS_DB_PATH` environment variable
points to the database file; it defaults to `../agnss.db` relative
to the `php/` directory.  Using `php/index.php` as the router script
(instead of `-t php/`) ensures POST requests to `/v1/location/agnss`
are routed correctly.

### Query parameters

| Parameter       | Values                               | Default |
|-----------------|--------------------------------------|---------|
| `types`         | `ephe,alm,iono,utc,loc` (any combo)  | all     |
| `prn`           | Comma-separated PRN numbers          | all     |
| `constellation` | `GPS`, `QZSS`                        | all     |
| `dataset`       | Metadata ID                          | latest  |

### curl examples

```sh
# API help — shows all parameters, values, and examples
curl -s http://localhost:8080/ | jq .

# Fetch all data types (latest dataset)
curl -s 'http://localhost:8080/?types=ephe,alm,iono,utc,loc' | jq .

# Ephemeris + ionosphere only
curl -s 'http://localhost:8080/?types=ephe,iono' | jq .

# Single satellite (PRN 1)
curl -s 'http://localhost:8080/?types=ephe&prn=1' | jq .

# Multiple PRNs
curl -s 'http://localhost:8080/?types=ephe&prn=1,3,7,14' | jq .

# QZSS satellites only
curl -s 'http://localhost:8080/?types=ephe&constellation=QZSS' | jq .

# GPS ephemeris + almanac for PRN 1
curl -s 'http://localhost:8080/?types=ephe,alm&prn=1' | jq .

# Only UTC and ionospheric correction parameters
curl -s 'http://localhost:8080/?types=utc,iono' | jq .

# Reference location only
curl -s 'http://localhost:8080/?types=loc' | jq .

# Specific historical dataset by ID
curl -s 'http://localhost:8080/?dataset=1' | jq .

# Count satellites in latest dataset
curl -s 'http://localhost:8080/?types=ephe' | jq '.ephemeris | length'

# List all GPS PRNs
curl -s 'http://localhost:8080/?types=ephe&constellation=GPS' \
  | jq '[.ephemeris[].prn]'

# Check dataset age
curl -s 'http://localhost:8080/?types=loc' | jq '.dataset.created_at'
```

### Example response

```sh
$ curl -s 'http://localhost:8080/?types=ephe,iono&prn=1' | jq .
```

```json
{
  "dataset": {
    "id": 1,
    "timestamp": 1773064800,
    "gps_week": 2409,
    "source": "BRDC00IGS_R_20260690000_01D_MN.rnx.gz",
    "created_at": "2026-03-11 02:00:00"
  },
  "ephemeris": [
    {
      "constellation": "GPS",
      "prn": 1,
      "health": 0,
      "iodc": 933,
      "iode": 165,
      "week": 2409,
      "toe": 396000,
      "toc": 396000,
      "af0": -2.578310668468e-5,
      "af1": -2.84217094304e-14,
      "af2": 0,
      "sqrt_a": 5153.64892578125,
      "e": 0.005483717424795,
      "i0": 0.975550898194,
      "omega0": -2.025233279608,
      "omega": -1.722784098283,
      "m0": 1.296410068666,
      "delta_n": 4.200289988741e-9,
      "omega_dot": -7.793218395069e-9,
      "idot": -2.07876751645e-10,
      "cuc": -2.935528755188e-6,
      "cus": 8.16285610199e-6,
      "crc": 222.65625,
      "crs": -48.0625,
      "cic": 1.043081283569e-7,
      "cis": 1.303851604462e-7,
      "tgd": -1.117587089539e-8
    }
  ],
  "ionosphere": {
    "alpha": [1.118e-8, 0, -5.961e-8, 0],
    "beta": [90110, 0, -196600, 0]
  }
}
```

### Deployment

For production, use PHP-FPM with nginx:

```nginx
server {
    listen 8080;
    root /path/to/rinex/php;
    index index.php;

    location / {
        try_files $uri /index.php?$query_string;
    }

    location ~ \.php$ {
        fastcgi_pass unix:/run/php/php-fpm.sock;
        fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
        fastcgi_param AGNSS_DB_PATH /var/lib/agnss/rinex.db;
        include fastcgi_params;
    }
}
```

## nRF Cloud-compatible binary A-GNSS endpoint

The server also exposes a `POST /v1/location/agnss` endpoint that returns
binary A-GNSS data in nRF Cloud schema v1 format. This is wire-compatible
with the `nrf_cloud_agnss` Zephyr library — existing firmware that uses
`nrf_cloud_agnss_process()` works unmodified, just by pointing it at this
server instead of `api.nrfcloud.com`.

### Binary protocol

The response is `application/octet-stream` with this layout:

```
[0x01]                     schema version
[type(1) count(2LE) data]  element group, repeated
```

Each element group carries a type ID and `count` packed structs. The
structs match the nRF modem `nrf_modem_gnss_agnss_*` types exactly
(little-endian, `__packed`).

| Type ID | Name               | Struct size (bytes) |
|---------|--------------------|---------------------|
| 1       | UTC parameters     | 14                  |
| 2       | GPS ephemeris      | 62                  |
| 3       | GPS almanac        | 31                  |
| 4       | Klobuchar iono     | 8                   |
| 7       | System clock       | 12                  |
| 8       | Location           | 15                  |
| 9       | Integrity (GPS)    | 4                   |
| 12      | QZSS ephemeris     | 62                  |
| 13      | QZSS integrity     | 4                   |

### curl examples

```sh
# Request all A-GNSS data types
curl -s -X POST http://localhost:8080/v1/location/agnss \
  -H 'Content-Type: application/json' \
  -d '{"types":[1,2,3,4,7,8,9]}' \
  -o agnss.bin

# GPS ephemeris + ionosphere only
curl -s -X POST http://localhost:8080/v1/location/agnss \
  -H 'Content-Type: application/json' \
  -d '{"types":[2,4]}' \
  -o agnss.bin

# With QZSS
curl -s -X POST http://localhost:8080/v1/location/agnss \
  -H 'Content-Type: application/json' \
  -d '{"types":[1,2,3,4,7,8,9,12,13]}' \
  -o agnss.bin

# Inspect response size
curl -s -X POST http://localhost:8080/v1/location/agnss \
  -H 'Content-Type: application/json' \
  -d '{"types":[1,2,3,4,7,8,9]}' \
  -w '\nSize: %{size_download} bytes\n' -o /dev/null

# Range request (fragmented delivery)
curl -s -X POST http://localhost:8080/v1/location/agnss \
  -H 'Content-Type: application/json' \
  -H 'Range: bytes=0-255' \
  -d '{"types":[2]}' \
  -o fragment.bin
```

### Firmware configuration

To point your nRF Connect SDK firmware at this server instead of
nRF Cloud, set these Kconfig options:

```
CONFIG_NRF_CLOUD_REST_AUTOGEN_JWT=n
CONFIG_NRF_CLOUD_REST_HOST_NAME="your-server:8080"
```

Or use the `nrf_cloud_rest` API with a custom hostname.

## LPP UPER encoder (3GPP TS 37.355)

The `lpp_builder` library encodes GPS/QZSS assistance data into 3GPP
TS 37.355 LPP `ProvideAssistanceData` messages using UPER (Unaligned
Packed Encoding Rules). This is the standard encoding used by LTE
modems for network-assisted GNSS via the LPP protocol.

### ASN.1 codec

See the [Build](#supl-server-and-client) section for asn1c installation
and codec generation. The generated C/H files are placed in
`builddir/asn1/generated{,-ulp}/` at configure time from the ASN.1
specs:

- `asn1/specs/LPP.asn` — 3GPP TS 37.355 Release 16.4 (LPP)
- `asn1/specs/ulp/*.asn` — OMA SUPL 2.0 (ULP)

### What it encodes

The encoder builds a complete LPP message from `gps_assist_data`:

| LPP IE                      | Source                                      |
|-----------------------------|---------------------------------------------|
| NAV ephemeris (per GPS SV)  | RINEX broadcast ephemeris, GPS ICD scales   |
| NAV ephemeris (per QZSS SV) | RINEX QZSS records, 0-based SV-ID from 193  |
| NAV almanac                 | SEM/YUMA parsed almanac                     |
| Klobuchar ionospheric model | RINEX header GPSA/GPSB                      |
| UTC ModelSet1               | RINEX header GPUT                           |
| GNSS reference time         | GPS days/seconds since epoch                |
| Reference location          | Ellipsoid point with altitude + uncertainty |

The scale factors match IS-GPS-200 Table 20-I/III/IX/X exactly — the
same conversions used by the nRF modem binary format.

### Testing

```sh
meson test -C builddir lpp -v
```

The test encodes known assistance data (2 GPS SVs, 1 QZSS SV,
almanac, ionosphere, UTC, location) to UPER, decodes back, and
verifies all 66 fields match. An empty-data test validates encoding
without satellite data.

## SUPL 2.0 server

The `supl_server` implements an OMA SUPL 2.0 Location Platform (SLP) that
serves A-GNSS assistance data to modems using the standard SUPL/LPP
protocol. It reads ephemeris, almanac, ionospheric and UTC data from a
SQLite database populated by `rinex_dl -s`.

### Running

```sh
# 1. Populate the database
./builddir/rinex_dl -s agnss.db

# 2. Start the SUPL server (plain TCP, port 7276)
./builddir/supl_server -d agnss.db -p 7276

# With TLS (requires certificate and key)
./builddir/supl_server -d agnss.db -c cert.pem -k key.pem
```

The server handles the full SUPL session flow: SUPL START → RESPONSE →
POS INIT → POS (LPP ProvideAssistanceData) → END. The LPP payload
includes GPS/QZSS ephemeris, Klobuchar ionosphere, UTC model, reference
time and reference location.

### SUPL diagnostic client

The `tests/supl_client` is a standalone SUPL 2.0 client for testing and
validation. It runs a complete SUPL session and optionally exports the
decoded LPP assistance data as JSON.

```sh
# Test against local server (plain TCP)
./builddir/tests/supl_client -h 127.0.0.1 -p 7276

# Test against Google SUPL (TLS)
./builddir/tests/supl_client -h supl.google.com -p 7275 -t

# Export decoded LPP data to JSON
./builddir/tests/supl_client -h 127.0.0.1 -p 7276 -o assist.json
```

The JSON output contains `commonAssistData` (reference time, reference
location, ionospheric model) and `genericAssistData` (per-GNSS navigation
models, UTC model) — the same structure for both local and third-party
SUPL servers.

## Zephyr integration

Copy these files into your Zephyr project `src/` directory:

| File                    | Role                                                                     |
|-------------------------|--------------------------------------------------------------------------|
| `lib/gps_assist.h`      | Struct definitions (portable, no Zephyr dependency)                      |
| `lib/gps_assist_nrf.h`  | Injection API declaration                                                |
| `lib/gps_assist_nrf.c`  | Converts doubles to GPS ICD format, calls `nrf_modem_gnss_agnss_write()` |
| `gps_assist_data.c`     | Generated — const ephemeris, iono, UTC data                              |

In your firmware, after modem initialisation and before starting a GNSS fix:

```c
#include "gps_assist.h"
#include "gps_assist_nrf.h"

int err = gps_assist_inject(&gps_assist);
if (err) {
    LOG_ERR("A-GNSS injection failed: %d", err);
}
```

The `gps_assist_inject()` function calls `nrf_modem_gnss_agnss_write()`
for each satellite ephemeris, the Klobuchar ionospheric model, UTC
parameters, system time, and location (if valid).

### Callback-driven selective injection

For production firmware, prefer `gps_assist_inject_from_request()` which
only injects what the modem actually needs:

```c
static void gnss_event_handler(int event)
{
    if (event == NRF_MODEM_GNSS_EVT_AGNSS_REQ) {
        struct nrf_modem_gnss_agnss_data_frame req;
        nrf_modem_gnss_read(&req, sizeof(req),
                            NRF_MODEM_GNSS_DATA_AGNSS_REQ);
        gps_assist_inject_from_request(&gps_assist, &req);
    }
}
```

Per-type injection functions (`gps_assist_inject_ephemeris()`,
`gps_assist_inject_utc()`, etc.) are also available for fine-grained
control.

### Expiry-aware refresh

`gps_assist_check_expiry()` queries the modem for what data has expired
and re-injects only the stale items:

```c
/* Periodically (e.g. every 30 min), or before starting a new fix */
int err = gps_assist_check_expiry(&gps_assist);
if (err)
    LOG_ERR("Expiry refresh failed: %d", err);
```

This calls `nrf_modem_gnss_agnss_expiry_get()` under the hood. Any
ephemeris with `ephe_expiry == 0` is re-injected, along with UTC,
Klobuchar, system time, or position data flagged in `data_flags`.

## Why this should work for the nRF9151

The nRF9151 modem exposes `nrf_modem_gnss_agnss_write()` which accepts
assistance data in the standard GPS ICD format (IS-GPS-200). This is the
same data that GPS satellites broadcast in their navigation message
subframes — the orbital parameters (ephemeris), clock corrections,
ionospheric model, and UTC offset.

RINEX broadcast navigation files (BRDC) are an ASCII dump of exactly
these navigation messages, collected by worldwide IGS ground stations.
The data content is identical; only the container format differs:

|              | Over the air      | SUPL/LPP (cellular) | This tool         |
|--------------|-------------------|---------------------|-------------------|
| Data content | GPS nav subframes | GPS nav subframes   | GPS nav subframes |
| Container    | L1 C/A signal     | ASN.1/LPP over LTE | RINEX → C source  |
| Delivery     | RF broadcast      | Network-assisted    | Compile-time      |

The conversion path is:

```
RINEX (doubles, radians) -> GPS ICD (scaled integers, semi-circles)
                         -> nrf_modem_gnss_agnss_write()
```

The scale factors used in `lib/gps_assist_nrf.c` match IS-GPS-200
Table 20-I/III/IX/X exactly (e.g. M0 is scaled by 1/2<<31 semi-circles,
af0 by 1/2<<31 seconds, eccentricity by 1/2<<33, etc.).

### What assistance data is provided

| Type               | nRF modem constant                                      | Source                             |
|--------------------|---------------------------------------------------------|------------------------------------|
| Ephemeris (per SV) | `NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES`                  | RINEX nav records                  |
| Klobuchar iono     | `NRF_MODEM_GNSS_AGNSS_KLOBUCHAR_IONOSPHERIC_CORRECTION` | RINEX header `GPSA`/`GPSB`         |
| UTC parameters     | `NRF_MODEM_GNSS_AGNSS_GPS_UTC_PARAMETERS`               | RINEX header `GPUT`                |
| GPS system time    | `NRF_MODEM_GNSS_AGNSS_GPS_SYSTEM_CLOCK_AND_TOWS`        | Generation timestamp               |
| Almanac (per SV)   | `NRF_MODEM_GNSS_AGNSS_GPS_ALMANAC`                      | SEM/YUMA or derived from ephemeris |
| Location           | `NRF_MODEM_GNSS_AGNSS_LOCATION`                         | Optional `-l LAT,LON` hint         |

## Tests

```sh
# Unit tests (no network required)
meson test -C builddir --no-suite integration --no-suite lint -v

# Integration tests (downloads from BKG IGS)
meson test -C builddir --suite integration -v

# PHP lint and static analysis
composer install --working-dir=php
meson test -C builddir --suite lint -v

# All tests
meson test -C builddir -v
```

The `supl_compare` integration test runs a full end-to-end validation: downloads BRDC
data, starts the local SUPL server, runs the client against both
`127.0.0.1` and `supl.google.com`, then compares the decoded LPP JSON
outputs for structural consistency (schema, SV counts, model types, leap
seconds). Network failures cause a graceful skip, not a test failure.

### test_rinex

Parses an embedded RINEX v3 fixture containing one GPS and one GLONASS
satellite. Verifies:

- Correct satellite count (GPS only, GLONASS skipped)
- PRN, GPS week, toc/toe
- Clock parameters (af0, af1, af2)
- Full orbital parameters (all 7 broadcast orbit lines)
- Ionospheric alpha/beta from header
- UTC parameters and leap seconds from header

With `--integration`: downloads yesterday's BRDC file from BKG and
validates that 24-32 healthy GPS satellites are parsed with plausible
ephemeris ranges (sqrt_A, eccentricity, toe).

### test_nrf_convert

Tests the GPS ICD conversion using a mock `nrf_modem_gnss.h` (in
`tests/`) so `lib/gps_assist_nrf.c` compiles on the build host. Verifies:

- `nrf_modem_gnss_agnss_write()` is called with correct types
  (`EPHEMERIDES`, `KLOBUCHAR`, `UTC`) and buffer sizes
- Round-trip accuracy for all scaled-integer conversions (double ->
  GPS ICD integer -> double) within 1 LSB tolerance:
  af0/af1 (2^-31/2^-43), M0 (2^-31 semi-circles), eccentricity (2^-33),
  sqrt_A (2^-19), Crc/Crs (2^-5), Klobuchar alpha/beta, UTC A0/tot
- Multi-satellite injection (N ephemerides + 1 iono + 1 UTC)
- Parsed almanac preference over derived (ioda verification)

### test_almanac

Tests the SEM and YUMA almanac parsers using embedded fixture data:

- SEM: correct entry count, PRN, week/toa, all orbital fields
- YUMA: correct entry count, PRN, radians-to-semi-circles conversion
- YUMA/SEM agreement: matching dimensionless/time fields across formats

### test_sqlitedb

Tests the SQLite storage layer using in-memory test data:

- Store and read: all fields round-trip (metadata, ephemeris, almanac, iono, UTC)
- Multiple datasets: separate metadata IDs, independent ephemeris rows
- No almanac: empty almanac/QZSS tables when none provided

### test_lpp

Round-trip test for the LPP UPER encoder (requires `liblpp_asn1.so`):

- Encode: builds LPP ProvideAssistanceData from test data (2 GPS + 1 QZSS
  ephemeris, 1 almanac, Klobuchar iono, UTC, location)
- Decode: parses UPER bitstream back into ASN.1 structures
- Verify: all fields match — reference time, location, ionosphere,
  ephemeris clock/orbit, almanac, UTC model, QZSS SV-ID (0-based)
- Empty data: encodes/decodes with no satellites (no GenericAssistData)

### test_php_api

Tests the PHP REST API query functions directly (requires `php` CLI):

- Full query: all types returned with correct values
- Type filtering: request only iono, or ephe+utc, etc.
- PRN filtering: select specific satellites
- Constellation filtering: GPS-only or QZSS-only queries
- Missing database: proper error handling
- JSON round-trip: encode/decode preserves all field precision

### test_nrf_cloud

Tests the nRF Cloud binary encoder functions (requires `php` CLI):

- Struct sizes: ephemeris (62 bytes), almanac (31 bytes), klobuchar (8),
  UTC (14), system time (12), location (15), integrity (4)
- Element group headers: correct type IDs and element counts
- GPS ICD conversions: toc/toe scaling, eccentricity quantization,
  Klobuchar alpha conversion, almanac time-of-almanac encoding
- Full response builder: schema byte, all element groups parseable,
  payload fully consumed
- Helper functions: to_unsigned masking for signed→unsigned packing
- Error handling: missing database exception

## Limitations

### No integrity data

Integrity data (`NRF_MODEM_GNSS_AGNSS_INTEGRITY`) is not provided.
GNSS signal integrity (UDREI per satellite) comes from SBAS systems
(WAAS/EGNOS) which broadcast real-time corrections via geostationary
satellites on L1. This data expires in seconds, making it unsuitable
for offline pre-generation. The nRF9151 modem decodes SBAS integrity
messages directly when SBAS satellites are visible — no injection needed.

### SQLite database output

With `-s DB_PATH`, data is stored in a SQLite database instead of a C
source file. Each run inserts a new dataset with its own metadata ID.
Tables: `metadata`, `ephemeris`, `almanac`, `ionosphere`, `utc_params`.
The database uses WAL mode for concurrent reads from serving layers
(PHP, C/libevent).

### GPS + QZSS only

GPS (`G`) and QZSS (`J`) satellites are parsed. GLONASS, Galileo,
and BeiDou records in the RINEX file are skipped. QZSS satellites
(PRN 193-202) share the GPS ephemeris/almanac structs and are
injected using the same `nrf_modem_gnss_agnss_write()` types.
