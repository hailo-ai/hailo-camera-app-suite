#!/bin/bash
set -e

function init_variables() {
    print_help_if_needed $@
    script_dir=$(dirname $(realpath "$0"))
    source $script_dir/../../../../../scripts/misc/checks_before_run.sh

    readonly RESOURCES_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/multi_person_multi_camera_tracking/resources"
    readonly POSTPROCESS_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/libs/post_processes/"
    readonly POSTPROCESS_SO="$POSTPROCESS_DIR/libyolo_post.so"
    readonly HEF_PATH="$RESOURCES_DIR/yolov5s_personface.hef"
    readonly FUNCTION_NAME="yolov5_personface"
    readonly DEFAULT_JSON_CONFIG_PATH="$RESOURCES_DIR/configs/yolov5_personface.json"
    readonly WHOLE_BUFFER_CROP_SO="$POSTPROCESS_DIR/cropping_algorithms/libwhole_buffer.so"


    video_sink_element=$([ "$XV_SUPPORTED" = "true" ] && echo "xvimagesink" || echo "ximagesink")
    print_gst_launch_only=false
    json_config_path=$DEFAULT_JSON_CONFIG_PATH
}

function print_usage() {
    echo "RE-ID app hailo - pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help                          Show this help"
    echo "  --print-gst-launch              Print the ready gst-launch command without running it"
    exit 0
}

function print_help_if_needed() {
    while test $# -gt 0; do
        if [ "$1" = "--help" ] || [ "$1" == "-h" ]; then
            print_usage
        fi

        shift
    done
}

function parse_args() {
    while test $# -gt 0; do
        if [ "$1" = "--help" ] || [ "$1" == "-h" ]; then
            print_usage
            exit 0
        elif [ "$1" = "--print-gst-launch" ]; then
            print_gst_launch_only=true
        else
            echo "Received invalid argument: $1. See expected arguments below:"
            print_usage
            exit 1
        fi
        shift
    done
}

function main() {
    init_variables $@
    parse_args $@

    DETECTION_PIPELINE="queue name=hailo_pre_cropper1_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailocropper so-path=$WHOLE_BUFFER_CROP_SO function-name=create_crops use-letterbox=true resize-method=bicubic internal-offset=true name=cropper1 \
        hailoaggregator name=agg1 \
        cropper1. ! queue name=bypess1_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! fakesink \
        cropper1. ! queue name=hailo_pre_detector_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        video/x-raw, format=RGB, width=640, height=640 ! \
        queue name=detector_pre_agg_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        pngenc qos=false ! multifilesink index=1 location=$TAPPAS_WORKSPACE/tappas/image_preproc_%d_.png"

    pipeline="gst-launch-1.0 \
        filesrc num-buffers=100 location=$RESOURCES_DIR/reid0.mp4 name=source_0 ! decodebin ! \
        queue name=hailo_preprocess_q_$n leaky=no max_size_buffers=5 max-size-bytes=0 max-size-time=0 ! \
        queue name=hailo_pre_convert_0 leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        videoconvert n-threads=8 qos=false ! video/x-raw,format=RGB ! \
        $DETECTION_PIPELINE "

    echo ${pipeline}
    if [ "$print_gst_launch_only" = true ]; then
        exit 0
    fi

    echo "Running Pipeline..."
    eval "${pipeline}"

}

main $@
