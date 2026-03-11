#!/usr/bin/env python3
"""Structural comparison of SUPL LPP assistance data JSON outputs.

Validates that LPP assistance data from a local SUPL server is
structurally consistent with a reference (e.g. supl.google.com).

Usage:
    compare_supl_json.py LOCAL.json [GOOGLE.json]

Exit codes:
    0 = all checks passed
    1 = structural mismatch (test failure)
    2 = usage error
"""
# Copyright (C) 2026 Free Mobile
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import sys

passed = 0
failed = 0


def check(cond, msg):
    global passed, failed
    if cond:
        passed += 1
        print(f"  ok: {msg}")
    else:
        failed += 1
        print(f"  FAIL: {msg}")
    return cond


def find_gnss(generic_list, name):
    for elem in generic_list:
        if elem.get("gnss") == name:
            return elem
    return None


def validate(data, label):
    """Structural checks on one JSON file."""
    print(f"\n=== {label}: structural validation ===")

    # Top-level
    check("commonAssistData" in data, "has commonAssistData")
    check("genericAssistData" in data, "has genericAssistData")

    common = data.get("commonAssistData", {})

    # Common assist data
    check("referenceTime" in common, "common: has referenceTime")
    check("referenceLocation" in common, "common: has referenceLocation")
    check("ionosphericModel" in common, "common: has ionosphericModel")

    rt = common.get("referenceTime", {})
    check(rt.get("gnss") == "GPS", "referenceTime.gnss == GPS")
    check(isinstance(rt.get("day"), (int, float)), "referenceTime.day is numeric")
    check(isinstance(rt.get("tod"), (int, float)), "referenceTime.tod is numeric")

    rl = common.get("referenceLocation", {})
    check(isinstance(rl.get("latitude"), (int, float)),
          "referenceLocation.latitude is numeric")
    check(isinstance(rl.get("longitude"), (int, float)),
          "referenceLocation.longitude is numeric")

    iono = common.get("ionosphericModel", {})
    iono_keys = {"alfa0", "alfa1", "alfa2", "alfa3",
                 "beta0", "beta1", "beta2", "beta3"}
    check(iono_keys.issubset(set(iono.keys())),
          f"ionosphericModel has all 8 Klobuchar params")

    # Generic assist data
    generic = data.get("genericAssistData", [])
    check(isinstance(generic, list) and len(generic) >= 1,
          "genericAssistData is non-empty array")

    gps = find_gnss(generic, "GPS")
    check(gps is not None, "genericAssistData has GPS element")
    if not gps:
        return

    # GPS navigation model
    nav = gps.get("navigationModel")
    check(nav is not None, "GPS has navigationModel")
    if nav:
        count = nav.get("count", 0)
        check(count >= 24, f"GPS navigationModel.count={count} >= 24")

        svs = nav.get("satellites", [])
        check(len(svs) >= 24, f"GPS satellites array len={len(svs)} >= 24")

        sv_ids = set()
        all_nav_clock = True
        all_nav_orbit = True
        for sv in svs:
            svid = sv.get("svId")
            if isinstance(svid, (int, float)):
                sv_ids.add(int(svid))
            if sv.get("clock") != "nav":
                all_nav_clock = False
            if sv.get("orbit") != "nav":
                all_nav_orbit = False

        check(all_nav_clock, "all GPS SVs use nav clock model")
        check(all_nav_orbit, "all GPS SVs use nav orbit model")
        check(len(sv_ids) >= 24,
              f"GPS has {len(sv_ids)} distinct SV IDs (>= 24)")

    # GPS UTC model
    utc = gps.get("utcModel")
    check(utc is not None, "GPS has utcModel")
    if utc:
        check(utc.get("model") == "set1", "utcModel.model == set1")
        check(isinstance(utc.get("deltaTls"), (int, float)),
              "utcModel.deltaTls is numeric")
        check(isinstance(utc.get("deltaTlsf"), (int, float)),
              "utcModel.deltaTlsf is numeric")


def cross_validate(local, google):
    """Cross-server structural comparison."""
    print("\n=== Cross-server comparison ===")

    lg = local.get("genericAssistData", [])
    gg = google.get("genericAssistData", [])
    local_gps = find_gnss(lg, "GPS")
    google_gps = find_gnss(gg, "GPS")

    if not local_gps or not google_gps:
        check(False, "both have GPS (required for cross-comparison)")
        return

    # Navigation model consistency
    ln = local_gps.get("navigationModel", {})
    gn = google_gps.get("navigationModel", {})
    lc = ln.get("count", 0)
    gc = gn.get("count", 0)
    check(lc >= 24 and gc >= 24,
          f"GPS nav count: local={lc}, google={gc} (both >= 24)")

    # UTC model consistency
    lu = local_gps.get("utcModel", {})
    gu = google_gps.get("utcModel", {})
    check(lu.get("model") == gu.get("model"),
          f"UTC model type: local={lu.get('model')}, "
          f"google={gu.get('model')}")

    # Leap seconds must match (stable physical constant)
    l_dtls = lu.get("deltaTls")
    g_dtls = gu.get("deltaTls")
    check(l_dtls == g_dtls,
          f"deltaTls: local={l_dtls}, google={g_dtls}")

    l_dtlsf = lu.get("deltaTlsf")
    g_dtlsf = gu.get("deltaTlsf")
    check(l_dtlsf == g_dtlsf,
          f"deltaTlsf: local={l_dtlsf}, google={g_dtlsf}")

    # Common assist data: both should have same core fields
    lc_keys = set(local.get("commonAssistData", {}).keys())
    gc_keys = set(google.get("commonAssistData", {}).keys())
    core = {"referenceTime", "ionosphericModel"}
    check(core.issubset(lc_keys) and core.issubset(gc_keys),
          "both have referenceTime + ionosphericModel")

    # Reference location (both should have it when client sends position)
    check("referenceLocation" in lc_keys and "referenceLocation" in gc_keys,
          "both have referenceLocation")


def main():
    if len(sys.argv) < 2:
        print("Usage: compare_supl_json.py LOCAL.json [GOOGLE.json]")
        sys.exit(2)

    with open(sys.argv[1]) as f:
        local = json.load(f)

    validate(local, "local")

    if len(sys.argv) >= 3:
        with open(sys.argv[2]) as f:
            google = json.load(f)
        validate(google, "google")
        cross_validate(local, google)
    else:
        print("\n(Google JSON not provided, skipping cross-comparison)")

    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1 if failed > 0 else 0)


if __name__ == "__main__":
    main()
