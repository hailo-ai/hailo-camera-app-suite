#!/bin/bash
set -e

function init_variables() {
    print_help_if_needed $@
    script_dir=$(dirname $(realpath "$0"))
    source $script_dir/../../../../../scripts/misc/checks_before_run.sh

    readonly RESOURCES_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/multistream_multidevice/resources"
    readonly DETECTION_HEF_PATH="$RESOURCES_DIR/ssd_mobilenet_v1.hef"
    readonly POSE_ESTIMATION_HEF_PATH="$RESOURCES_DIR/joined_centerpose_repvgg_a0_center_nms_joint_nms.hef"

    readonly POSTPROCESS_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/libs/post_processes/"
    readonly DETECTION_POSTPROCESS_SO="$POSTPROCESS_DIR/libyolo_post.so"
    readonly POSE_ESTIMATION_POSTPROCESS_SO="$POSTPROCESS_DIR/libcenterpose_post.so"
    readonly POSE_ESTIMATION_POSTPROCESS_FUNCTION_NAME="centerpose_416"
    readonly DETECTION_POSTPROCESS_FUNCTION_NAME="yolov5_no_persons"
    readonly STREAM_DISPLAY_SIZE=300
    readonly DEFAULT_JSON_CONFIG_PATH="$RESOURCES_DIR/configs/yolov5.json" 

    num_of_src=8
    debug=false
    gst_top_command=""
    additional_parameters=""
    rtsp_sources=""
    print_gst_launch_only=false
    json_config_path=$DEFAULT_JSON_CONFIG_PATH 

    video_sink_element=$([ "$XV_SUPPORTED" = "true" ] && echo "xvimagesink" || echo "ximagesink")
}

function print_usage() {
    echo "RTSP Detection and Pose Estimation - pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help                  Show this help"
    echo "  --debug                 Setting debug mode. using gst-top to print time and memory consuming elements"
    echo "  --show-fps              Printing fps"
    echo "  --num-of-sources NUM    Setting number of rtsp sources to given input (default value is 8)"
    echo "  --print-gst-launch      Print the ready gst-launch command without running it"
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
        if [ "$1" = "--debug" ]; then
            echo "Setting debug mode. using gst-top to print time and memory consuming elements"
            debug=true
            gst_top_command="SHOW_FPS=1 GST_DEBUG=GST_TRACER:13 GST_DEBUG_DUMP_TRACE_DIR=. gst-top-1.0"
        elif [ "$1" = "--print-gst-launch" ]; then
            print_gst_launch_only=true
        elif [ "$1" = "--show-fps" ]; then
            echo "Printing fps"
            additional_parameters="-v | grep hailo_display"
        elif [ "$1" = "--num-of-sources" ]; then
            shift
            echo "Setting number of rtsp sources to $1"
            num_of_src=$1
        else
            echo "Received invalid argument: $1. See expected arguments below:"
            print_usage
            exit 1
        fi
        shift
    done
}

function create_sources() {
    for ((n = 0; n < $num_of_src; n++)); do
        src_name="SRC_${n}"
        src_name="${!src_name}"
        sources+="videotestsrc name=test_src_$n ! video/x-raw, width=1080, height=720 ! \
                        queue name=hailo_preconvert_q_$n leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
                        videoconvert qos=false ! \
                        queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
                        videoscale qos=false ! video/x-raw, width=300, height=300 ! \
                        queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
                        fun.sink_$n "
    done
}

function main() {
    init_variables $@
    parse_args $@

    create_sources

    pipeline="gst-launch-1.0 \
         funnel name=fun ! video/x-raw, width=300, height=300 ! \
         queue name=hailo_pre_split leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
         hailonet hef-path=$DETECTION_HEF_PATH scheduling-algorithm=0 is-active=true vdevice-key=1 ! \
         queue name=hailo_postprocess0 leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
         hailonet hef-path=$DETECTION_HEF_PATH scheduling-algorithm=0 is-active=true vdevice-key=2 ! \
         queue name=hailo_postprocess1 leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
         hailonet hef-path=$DETECTION_HEF_PATH scheduling-algorithm=0 is-active=true vdevice-key=3 ! \
         queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
         fakesink sync=false \
         $sources ${additional_parameters}"

    echo ${pipeline}
    if [ "$print_gst_launch_only" = true ]; then
        exit 0
    fi

    echo "Running Pipeline..."
    eval "${pipeline}"
}

main $@
