################################################
# HAILO 15 CPP APPLICATIONS
################################################

frontend_update_src = ['frontend_update.cpp', '../apps_common.cpp']

executable('frontend_update',
  frontend_update_src,
  cpp_args : hailo_lib_args,
  include_directories: [include_directories('../')],
  dependencies : plugin_deps + [tappas_general_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: apps_install_dir + '/vision_preproc',
)

dynamic_changes_update_src = ['dynamic_changes_update.cpp', '../apps_common.cpp']
executable('dynamic_changes_update',
  dynamic_changes_update_src,
  cpp_args : hailo_lib_args,
  include_directories: [include_directories('../')],
  dependencies : plugin_deps + [tappas_general_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: apps_install_dir + '/vision_preproc',)

