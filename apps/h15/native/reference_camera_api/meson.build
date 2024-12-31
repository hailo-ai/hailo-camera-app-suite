################################################
# Infra API Libs
################################################
reference_camera_infra_src = 'reference_camera_infra.cpp'

reference_camera_infra_lib = shared_library('hailo_reference_camera',
  reference_camera_infra_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, opencv_dep, tracker_dep, tappas_general_dep, image_dep],
  gnu_symbol_visibility : 'default',
  version: meson.project_version(),
  install: true,
  install_dir: get_option('libdir'),
)

reference_camera_dep = declare_dependency(
  include_directories: [include_directories('pipeline_infra')],
  dependencies : [opencv_dep, tracker_dep],
  link_with : reference_camera_infra_lib)

pkgc.generate(
    name : 'reference_camera_api',
    libraries : reference_camera_infra_lib,
    subdirs : 'hailo/tappas/reference_camera',
    version : meson.project_version(),
    description : 'Hailo Tappas Reference Camera API',
    requires: ['opencv4', 'hailo_tracker'],
)

install_subdir('pipeline_infra', strip_directory: true, install_dir: get_option('includedir') + '/hailo/tappas/reference_camera')