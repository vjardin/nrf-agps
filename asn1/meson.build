# Copyright (C) 2026 Free Mobile
# SPDX-License-Identifier: AGPL-3.0-or-later

fs = import('fs')

asn1c = find_program('asn1c', required : false)
gen_script = files('generate.sh')

asn1_warn_args = [
  '-Wno-unused-parameter',
  '-Wno-missing-field-initializers',
  '-Wno-unused-variable',
]

asn1_common_args = ['-DASN_DISABLE_OER_SUPPORT']

lpp_gen_dir = meson.current_source_dir() / 'generated'
ulp_gen_dir = meson.current_source_dir() / 'generated-ulp'

lpp_specs = files('specs' / 'LPP.asn')

ulp_spec_names = run_command('ls', meson.current_source_dir() / 'specs' / 'ulp',
                             check : true).stdout().strip().split('\n')
ulp_spec_paths = []
foreach s : ulp_spec_names
  ulp_spec_paths += meson.current_source_dir() / 'specs' / 'ulp' / s
endforeach

# Auto-generate at configure time if directories are missing
if not fs.is_dir(lpp_gen_dir)
  if not asn1c.found()
    error('asn1/generated/ not found and asn1c is not available')
  endif
  message('Generating LPP ASN.1 sources...')
  run_command('sh', gen_script, lpp_gen_dir,
              meson.current_source_dir() / 'specs' / 'LPP.asn',
              check : true)
endif

if not fs.is_dir(ulp_gen_dir)
  if not asn1c.found()
    error('asn1/generated-ulp/ not found and asn1c is not available')
  endif
  message('Generating ULP ASN.1 sources...')
  run_command('sh', gen_script, ulp_gen_dir, ulp_spec_paths, check : true)
endif

# Glob generated .c files
lpp_sources = run_command('find', lpp_gen_dir, '-maxdepth', '1', '-name', '*.c',
                          check : true).stdout().strip().split('\n')
ulp_sources = run_command('find', ulp_gen_dir, '-maxdepth', '1', '-name', '*.c',
                          check : true).stdout().strip().split('\n')

liblpp_asn1 = shared_library('lpp_asn1',
  lpp_sources,
  c_args : asn1_common_args + asn1_warn_args,
  include_directories : include_directories('generated'),
)

libulp_asn1 = shared_library('ulp_asn1',
  ulp_sources,
  c_args : asn1_common_args + asn1_warn_args,
  include_directories : include_directories('generated-ulp'),
)

dep_lpp_asn1 = declare_dependency(
  link_with : liblpp_asn1,
  include_directories : include_directories('generated'),
  compile_args : asn1_common_args,
)

dep_ulp_asn1 = declare_dependency(
  link_with : libulp_asn1,
  include_directories : include_directories('generated-ulp'),
  compile_args : asn1_common_args,
)

# Explicit regeneration target: ninja regenerate-asn1
if asn1c.found()
  run_target('regenerate-asn1',
    command : ['sh', '-c',
      'sh @0@ @1@ @2@ && sh @0@ @3@ @4@'.format(
        meson.current_source_dir() / 'generate.sh',
        lpp_gen_dir,
        meson.current_source_dir() / 'specs' / 'LPP.asn',
        ulp_gen_dir,
        ' '.join(ulp_spec_paths),
      ),
    ],
  )
endif
