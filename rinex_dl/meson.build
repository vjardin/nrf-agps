rinex_dl = executable('rinex_dl',
  'rinex_main.c',
  lib_rinex_sources,
  include_directories : lib_inc,
  dependencies : [dep_curl, dep_sqlite3, dep_zlib, dep_m],
)
