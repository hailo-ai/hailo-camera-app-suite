# Hailort Arguments
sysroot_arg = []
if meson.is_cross_build()
    cmd_array = meson.get_compiler('cpp').cmd_array()
    foreach arg : cmd_array
        if arg.startswith('--sysroot=')
            sysroot = arg.split('=')[1]
            break
        endif
    endforeach
    message('Cross building using sysroot: ' + sysroot)
    if sysroot != ''
        sysroot_arg += '--sysroot=' + sysroot
    endif
endif

libhailort_dep = [meson.get_compiler('cpp').find_library('hailort', required: false)]

################################################
# HAILO 15 CHAOS APP
################################################

chaos_app_src = ['chaos_app.cpp']

executable('chaos_app',
  chaos_app_src,
  cpp_args : hailo_lib_args,
  include_directories:  [include_directories('../'), include_directories('../../../plugins/'), include_directories('../../../../../apps/h15/native/reference_camera_api/')],
  dependencies : dependencies_apps + [tappas_general_dep] + [libhailort_dep, opencv_dep, tracker_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: apps_install_dir + '/chaos_app',
)