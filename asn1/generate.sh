#!/bin/sh
# Generate ASN.1 C sources using asn1c (mouse07410/asn1c v1.4+).
#
# Usage: generate.sh <output-dir> <spec-files...>
#
# This wrapper exists because Meson's run_command() cannot change the
# working directory, and asn1c writes all output files into the current
# directory.  The script removes any previous generation, creates the
# output directory, cd's into it, then exec's asn1c with the project's
# standard flags.
#
# Called from asn1/meson.build:
#   - at configure time (run_command) when generated dirs are missing
#   - by the "regenerate-asn1" run_target (ninja regenerate-asn1)
set -e
OUTDIR="$1"; shift
rm -rf "$OUTDIR"
mkdir -p "$OUTDIR"
cd "$OUTDIR"
exec asn1c -fcompound-names -no-gen-OER -no-gen-CBOR -no-gen-APER \
     -no-gen-XER -no-gen-JER -no-gen-print -pdu=all -no-gen-example "$@"
