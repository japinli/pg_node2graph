# Entry point for building pg_node2graph with meson.
#
# Good starting points for writing meson.build files are:
#  - https://mesonbuild.com/Syntax.html
#  - https://mesonbuild.com/Reference-manual.html

project('pg_node2graph',
  'cpp',
  version: '0.2',
  default_options: [
    'buildtype=release',
    'prefix=/usr/local/bin',
  ]
)

cdata = configuration_data()

version = meson.project_version()
cdata.set_quoted('VERSION', version)

configure_file(output: 'config.h', configuration: cdata)

executable('pg_node2graph',
  'pg_node2graph.cc',
  cpp_args: ['-std=c++11']
)