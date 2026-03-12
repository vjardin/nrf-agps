# Copyright (C) 2026 Free Mobile
# SPDX-License-Identifier: AGPL-3.0-or-later

test_inc = include_directories('.')

# --- Unit tests ---

test_rinex = executable('test_rinex',
  'test_rinex.c',
  files('../lib/rinex.c'),
  include_directories : [lib_inc, test_inc],
  dependencies : [dep_curl, dep_sqlite3, dep_zlib, dep_m],
)
test('rinex', test_rinex)

test_nrf_convert = executable('test_nrf_convert',
  'test_nrf_convert.c',
  lib_nrf_sources,
  include_directories : [lib_inc, test_inc],
  dependencies : [dep_m],
)
test('nrf_convert', test_nrf_convert)

test_almanac = executable('test_almanac',
  'test_almanac.c',
  files('../lib/almanac.c'),
  include_directories : [lib_inc, test_inc],
  dependencies : [dep_curl, dep_sqlite3, dep_zlib, dep_m],
)
test('almanac', test_almanac)

test_sqlitedb = executable('test_sqlitedb',
  'test_sqlitedb.c',
  files('../lib/sqlitedb.c'),
  include_directories : [lib_inc, test_inc],
  dependencies : [dep_sqlite3],
)
test('sqlitedb', test_sqlitedb)

test_nrf_cloud_cross = executable('test_nrf_cloud_cross',
  'test_nrf_cloud_cross.c',
  lib_nrf_sources,
  include_directories : [lib_inc, test_inc],
  dependencies : [dep_m],
)

test_lpp = executable('test_lpp',
  'test_lpp.c',
  lib_lpp_builder_sources,
  include_directories : [lib_inc, test_inc],
  dependencies : [dep_lpp_asn1, dep_m],
)
test('lpp', test_lpp)

# --- SUPL client ---

supl_client_deps = [dep_lpp_asn1, dep_ulp_asn1, dep_openssl, dep_m]
if dep_cjson.found()
  supl_client_deps += dep_cjson
endif

if dep_openssl.found() and dep_cjson.found()
  supl_client = executable('supl_client',
    'supl_client.c',
    include_directories : [lib_inc, test_inc],
    dependencies : supl_client_deps,
  )
endif

# --- PHP tests ---

php = find_program('php', required : false)

if php.found()
  test('php_api', php,
    args : [meson.project_source_root() / 'tests' / 'test_php_api.php'],
    workdir : meson.project_source_root(),
  )

  test('php_nrf_cloud', php,
    args : [meson.project_source_root() / 'tests' / 'test_nrf_cloud.php'],
    workdir : meson.project_source_root(),
  )

  test('nrf_cloud_cross', find_program('sh'),
    args : ['-c',
      'TMPBIN=$(mktemp) && ' +
      'php @0@ "$TMPBIN" && '.format(
        meson.project_source_root() / 'tests' / 'gen_nrf_cloud_binary.php') +
      '@0@ "$TMPBIN" ; '.format(test_nrf_cloud_cross.full_path()) +
      'RC=$? ; rm -f "$TMPBIN" ; exit $RC',
    ],
    depends : [test_nrf_cloud_cross],
    workdir : meson.project_source_root(),
  )
endif

# --- Integration tests ---

test('rinex_integration', test_rinex,
  args : ['--integration'],
  suite : 'integration',
  timeout : 120,
)

# --- SUPL structural comparison ---

if dep_event.found() and dep_openssl.found() and dep_cjson.found()
  python3 = find_program('python3', required : false)
  if python3.found()
    test('supl_compare', find_program('sh'),
      args : [meson.project_source_root() / 'tests' / 'test_supl_compare.sh'],
      env : [
        'RINEX_DL=' + rinex_dl.full_path(),
        'SUPL_SERVER=' + supl_server.full_path(),
        'SUPL_CLIENT=' + supl_client.full_path(),
      ],
      depends : [rinex_dl, supl_server, supl_client],
      is_parallel : false,
      suite : 'integration',
      timeout : 120,
    )
  endif
endif

# --- PHP lint ---

if php.found() and fs.is_dir(meson.project_source_root() / 'php' / 'vendor')
  test('php_lint', find_program('sh'),
    args : ['-c',
      'php/vendor/bin/parallel-lint php/ tests/test_php_api.php tests/test_nrf_cloud.php && ' +
      'cd php && vendor/bin/phpstan analyse',
    ],
    workdir : meson.project_source_root(),
    suite : 'lint',
  )
endif
