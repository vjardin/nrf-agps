project('rinex', 'c',
  version : '1.0.0',
  license : 'AGPL-3.0-or-later',
  default_options : [
    'c_std=c11',
    'warning_level=2',
    'optimization=2',
  ],
)

add_project_arguments('-D_GNU_SOURCE', language : 'c')

cc = meson.get_compiler('c')

dep_curl    = dependency('libcurl')
dep_sqlite3 = dependency('sqlite3')
dep_zlib    = dependency('zlib')
dep_m       = cc.find_library('m', required : false)

dep_event     = dependency('libevent',         required : false)
dep_event_ssl = dependency('libevent_openssl', required : false)
dep_openssl   = dependency('openssl',          required : false)
dep_cjson     = dependency('libcjson',         required : false)

subdir('asn1')
subdir('lib')
subdir('rinex_dl')

if dep_event.found() and dep_openssl.found()
  subdir('supl')
endif

subdir('tests')
