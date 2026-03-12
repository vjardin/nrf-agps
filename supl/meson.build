# Copyright (C) 2026 Free Mobile
# SPDX-License-Identifier: AGPL-3.0-or-later

supl_server = executable('supl_server',
  'supl_main.c',
  'supl_server.c',
  lib_supl_sources,
  include_directories : [lib_inc, include_directories('.')],
  dependencies : [
    dep_lpp_asn1,
    dep_ulp_asn1,
    dep_sqlite3,
    dep_event,
    dep_event_ssl,
    dep_openssl,
    dep_m,
  ],
)
