# Add libdatachannel manually
libdatachannel_link = compiler.find_library('libdatachannel', required : true, dirs : '/usr/lib')

# Other DEPS
thread_dep = dependency('threads')
glib_dep = dependency('glib-2.0')
spdlog_dep = dependency('spdlog', version: '1.9.2')

# Add your source files here
sources = ['main.cpp', 
            'resources/encoder.cpp',
            'resources/osd.cpp',
            'resources/configs.cpp',
            'resources/frontend.cpp',
            'resources/ai.cpp',
            'resources/repository.cpp',
            'resources/isp.cpp',
            'resources/privacy_mask.cpp',
            'resources/webpage.cpp',
            'resources/webrtc.cpp',
            'pipeline/pipeline.cpp',
            'pipeline/dummy_pipeline.cpp',
            'pipeline/pipeline_interface.cpp',
            'common/isp/common.cpp',
            'common/httplib/httplib_utils.cpp',
            'common/webserver_logger.cpp'
]

executable('medialib-webserver',
  sources,
  gnu_symbol_visibility : 'default',
  dependencies : dependencies_apps + [thread_dep, glib_dep, spdlog_dep, libdatachannel_link],
  cpp_args: hailo_lib_args,
  install: true,
  install_dir: get_option('bindir'),
)