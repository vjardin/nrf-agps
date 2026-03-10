# rinex_dl — Offline A-GNSS data generator for nRF9151

[![CI](https://github.com/vjardin/nrf-agps/actions/workflows/ci.yml/badge.svg)](https://github.com/vjardin/nrf-agps/actions/workflows/ci.yml)

Downloads daily GPS broadcast ephemeris from public RINEX sources and
generates a C file with satellite orbital parameters. The generated file
is compiled and linked into a Zephyr firmware to provide Assisted GPS on
the nRF9151, providing helpers to reduce Time To First Fix (TTFF).

## Build

Dependencies: `libcurl-dev`, `zlib-dev`, `pkg-config`.

```
# Debian/Ubuntu
sudo apt install libcurl4-openssl-dev zlib1g-dev pkg-config

make
```

## Usage

```
./rinex_dl [options]

Options:
  -d YYYY-MM-DD  Date to download (default: yesterday)
  -o PREFIX      Output file prefix (default: gps_assist_data)
  -u URL         Custom RINEX navigation file URL
  -f FILE        Parse local RINEX file instead of downloading
  -h             Show this help
```

### Examples

```sh
# Download yesterday's broadcast ephemeris (default)
./rinex_dl

# Specific date
./rinex_dl -d 2026-03-08

# Parse a local RINEX file
./rinex_dl -f /path/to/BRDC00IGS_R_20260680000_01D_MN.rnx.gz

# Custom output path
./rinex_dl -o /path/to/zephyr_project/src/gps_assist_data
```

The tool downloads the combined BRDC navigation file from
[BKG IGS](https://igs.bkg.bund.de/) (no authentication required),
parses GPS satellite records, and writes `gps_assist_data.c`.

### Daily cron job

```
# /etc/cron.d/rinex-agps
# Run at 02:00 UTC daily — yesterday's BRDC file is complete by then
0 2 * * * user /path/to/rinex_dl -o /path/to/zephyr_project/src/gps_assist_data
```

## Zephyr integration

Copy these files into your Zephyr project `src/` directory:

| File | Role |
|------|------|
| `gps_assist.h` | Struct definitions (portable, no Zephyr dependency) |
| `gps_assist_nrf.h` | Injection API declaration |
| `gps_assist_nrf.c` | Converts doubles to GPS ICD format, calls `nrf_modem_gnss_agnss_write()` |
| `gps_assist_data.c` | Generated — const ephemeris, iono, UTC data |

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
for each satellite ephemeris, the Klobuchar ionospheric model, and the
UTC parameters.

## Why this should work for the nRF9151

The nRF9151 modem exposes `nrf_modem_gnss_agnss_write()` which accepts
assistance data in the standard GPS ICD format (IS-GPS-200). This is the
same data that GPS satellites broadcast in their navigation message
subframes — the orbital parameters (ephemeris), clock corrections,
ionospheric model, and UTC offset.

RINEX broadcast navigation files (BRDC) are an ASCII dump of exactly
these navigation messages, collected by worldwide IGS ground stations.
The data content is identical; only the container format differs:

| | Over the air | SUPL/LPP (cellular) | This tool |
|---|---|---|---|
| Data content | GPS nav subframes | GPS nav subframes | GPS nav subframes |
| Container | L1 C/A signal | ASN.1/LPP over LTE | RINEX → C source |
| Delivery | RF broadcast | Network-assisted | Compile-time |

The conversion path is:

```
RINEX (doubles, radians) -> GPS ICD (scaled integers, semi-circles)
                         -> nrf_modem_gnss_agnss_write()
```

The scale factors used in `gps_assist_nrf.c` match IS-GPS-200
Table 20-I/III/IX/X exactly (e.g. M0 is scaled by 1/2<<31 semi-circles,
af0 by 1/2<<31 seconds, eccentricity by 1/2<<33, etc.).

### What assistance data is provided

| Type | nRF modem constant | Source |
|------|-------------------|--------|
| Ephemeris (per SV) | `NRF_MODEM_GNSS_AGNSS_GPS_EPHEMERIDES` | RINEX nav records |
| Klobuchar iono | `NRF_MODEM_GNSS_AGNSS_KLOBUCHAR_IONOSPHERIC_CORRECTION` | RINEX header `GPSA`/`GPSB` |
| UTC parameters | `NRF_MODEM_GNSS_AGNSS_GPS_UTC_PARAMETERS` | RINEX header `GPUT` |
| GPS system time | `NRF_MODEM_GNSS_AGNSS_GPS_SYSTEM_CLOCK_AND_TOWS` | Generation timestamp |
| Location | `NRF_MODEM_GNSS_AGNSS_LOCATION` | Optional `-l LAT,LON` hint |

## Tests

```sh
# Unit tests (no network required)
make test

# Unit + integration tests (downloads from BKG IGS)
make test-integration
```

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
`tests/`) so `gps_assist_nrf.c` compiles on the build host. Verifies:

- `nrf_modem_gnss_agnss_write()` is called with correct types
  (`EPHEMERIDES`, `KLOBUCHAR`, `UTC`) and buffer sizes
- Round-trip accuracy for all scaled-integer conversions (double ->
  GPS ICD integer -> double) within 1 LSB tolerance:
  af0/af1 (2^-31/2^-43), M0 (2^-31 semi-circles), eccentricity (2^-33),
  sqrt_A (2^-19), Crc/Crs (2^-5), Klobuchar alpha/beta, UTC A0/tot
- Multi-satellite injection (N ephemerides + 1 iono + 1 UTC)

## Limitations

### No integrity data

Integrity data (`NRF_MODEM_GNSS_AGNSS_INTEGRITY`) is not provided yet.

### Compile-time data only

It is just for testing, then it can become a web service solution.

### GPS only

Only GPS (constellation `G`) satellites are parsed. GLONASS, Galileo,
and BeiDou records in the RINEX file are skipped. The nRF9151 modem
primarily uses GPS for A-GNSS assistance.
