#!/bin/bash
set -e

function init_variables() {
    print_help_if_needed $@
    script_dir=$(dirname $(realpath "$0"))
    source $script_dir/../../../../../scripts/misc/checks_before_run.sh

    readonly RESOURCES_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/multi_person_multi_camera_tracking/resources"
    readonly POSTPROCESS_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/libs/post_processes/"
    readonly APPS_LIBS_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/libs/apps/re_id/"
    readonly POSTPROCESS_SO="$POSTPROCESS_DIR/libyolo_post.so"
    readonly CROPPER_SO="$POSTPROCESS_DIR/cropping_algorithms/libre_id.so"
    readonly RE_ID_POST_SO="$POSTPROCESS_DIR/libre_id.so"
    readonly HEF_PATH="$RESOURCES_DIR/yolov5s_personface.hef"
    readonly REID_HEF_PATH="$RESOURCES_DIR/repvgg_a0_person_reid_2048.hef"
    readonly FUNCTION_NAME="yolov5_personface"
    readonly RE_ID_OVERLAY="$APPS_LIBS_DIR/libre_id_overlay.so"
    readonly DEFAULT_JSON_CONFIG_PATH="$RESOURCES_DIR/configs/yolov5_personface.json"
    readonly WHOLE_BUFFER_CROP_SO="$POSTPROCESS_DIR/cropping_algorithms/libwhole_buffer.so"


    video_sink_element=$([ "$XV_SUPPORTED" = "true" ] && echo "xvimagesink" || echo "ximagesink")
    num_of_src=4
    additional_parameters=""
    sources=""
    compositor_locations="sink_0::xpos=0 sink_0::ypos=0 sink_1::xpos=800 sink_1::ypos=0 sink_2::xpos=0 sink_2::ypos=450 sink_3::xpos=800 sink_3::ypos=450"
    print_gst_launch_only=false
    vdevice_key=1
    json_config_path=$DEFAULT_JSON_CONFIG_PATH
}

function print_usage() {
    echo "RE-ID app hailo - pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help                          Show this help"
    echo "  --show-fps                      Printing fps"
    echo "  --num-of-sources NUM            Setting number of sources to given input (default value is 4)"
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
        elif [ "$1" = "--show-fps" ]; then
            echo "Printing fps"
            additional_parameters="-v | grep hailo_display"
        elif [ "$1" = "--num-of-sources" ]; then
            shift
            echo "Setting number of sources to $1"
            num_of_src=$1
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
        cropper1. ! queue name=bypess1_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! agg1. \
        cropper1. ! queue name=hailo_pre_detector_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailonet hef-path=$HEF_PATH ! \
        queue name=detector_post_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailofilter so-path=$POSTPROCESS_SO qos=false function_name=$FUNCTION_NAME config-path=$json_config_path ! \
        queue name=detector_pre_agg_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        agg1. agg1."

    pipeline="gst-launch-1.0 \
        filesrc num-buffers=100 location=$RESOURCES_DIR/reid0.mp4 name=source_0 ! decodebin ! \
        queue name=hailo_preprocess_q_$n leaky=no max_size_buffers=5 max-size-bytes=0 max-size-time=0 ! \
        queue name=hailo_pre_convert_0 leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        videoconvert n-threads=8 qos=false ! video/x-raw,format=RGB ! \
        $DETECTION_PIPELINE ! \
        queue name=hailo_pre_track leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailotracker name=hailo_tracker class-id=1 kalman-dist-thr=0.7 iou-thr=0.7 init-iou-thr=0.8 \
        keep-new-frames=2 keep-tracked-frames=4 keep-lost-frames=8 ! \
        queue name=hailo_pre_draw leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
	hailopython module=$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/multi_person_multi_camera_tracking/json_saver.py qos=false ! \
        queue name=hailo_post_scale leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        videoscale n-threads=4 add-borders=false qos=false ! video/x-raw, width=800, height=450, pixel-aspect-ratio=1/1 ! \
        queue name=hailo_video_q_0 leaky=no max_size_buffers=30 max-size-bytes=0 max-size-time=0 ! \
        videoconvert n-threads=4 qos=false ! \
        queue name=hailo_display_q_0 leaky=no max_size_buffers=30 max-size-bytes=0 max-size-time=0 ! \
        fpsdisplaysink video-sink=xvimagesink name=hailo_display sync=false text-overlay=false \
        $sources ${additional_parameters}"

    echo ${pipeline}
    if [ "$print_gst_launch_only" = true ]; then
        exit 0
    fi

    echo "Running Pipeline..."
    eval "${pipeline}"

}

main $@
