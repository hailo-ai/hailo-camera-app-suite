################################################
# HAILO 15 CHAOS APP
################################################

chaos_app_src = ['chaos_app.cpp']

executable('chaos_app',
  chaos_app_src,
  cpp_args : hailo_lib_args,
  include_directories:  [include_directories('../')],
  dependencies : dependencies_apps + [tappas_general_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: apps_install_dir + '/chaos_app',
)