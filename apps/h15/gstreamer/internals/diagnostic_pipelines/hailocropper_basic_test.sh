#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    readonly RESOURCES_DIR="${CURRENT_DIR}/resources"
    readonly POSTPROCESS_DIR="/usr/lib/hailo-post-processes"
    readonly CROPPING_ALGORITHMS_DIR="$POSTPROCESS_DIR/cropping_algorithms"
    readonly DEFAULT_POSTPROCESS_SO="$POSTPROCESS_DIR/libdebug.so"
    readonly DEFAULT_VIDEO_SOURCE="${RESOURCES_DIR}/detection.png"
    readonly DEFAULT_JSON_CONFIG_PATH="$RESOURCES_DIR/configs/yolov5.json" 
    readonly WHOLE_CROP_SO="$CROPPING_ALGORITHMS_DIR/libwhole_buffer.so"
    readonly DETECTION_CROP_SO="$CROPPING_ALGORITHMS_DIR/libdetection_croppers.so"
    
    input_source=$DEFAULT_VIDEO_SOURCE
    hef_path=$DEFAULT_HEF_PATH
    json_config_path=$DEFAULT_JSON_CONFIG_PATH 

    debug_so=$DEFAULT_POSTPROCESS_SO
    center="generate_center_detection"
    random="generate_random_detections"
    bottom="generate_bottom_detection"

    print_gst_launch_only=false
    additional_parameters=""

    sink_pipeline="fpsdisplaysink fps-update-interval=2000 video-sink=autovideosink name=hailo_display sync=false text-overlay=false"
}

function print_usage() {
    echo "IMX8 Detection pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help              Show this help"
    echo "  -i INPUT --input INPUT          Set the video source (default $input_source)"
    echo "  --show-fps          Print fps"
    echo "  --print-gst-launch  Print the ready gst-launch command without running it"
    exit 0
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
        elif [ "$1" = "--save-image" ] || [ "$1" = "-i" ]; then
            sink_pipeline="jpegenc ! filesink location=cropper_test.jpeg"
            shift
        elif [ "$1" = "--input" ] || [ "$1" = "-i" ]; then
            input_source="$2"
            shift
        else
            echo "Received invalid argument: $1. See expected arguments below:"
            print_usage
            exit 1
        fi

        shift
    done
}

init_variables $@

parse_args $@
internal_offset=true

PIPELINE="gst-launch-1.0 \
    filesrc location=$input_source ! decodebin ! videoconvert !
    queue leaky=downstream max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailofilter so-path=$debug_so function-name=$bottom qos=false ! \
        hailocropper so-path=$DETECTION_CROP_SO function-name=all_detections internal-offset=$internal_offset drop-uncropped-buffers=true name=cropper1 \
        hailoaggregator name=agg1 flatten-detections=false \
        cropper1. ! \
            queue leaky=no max-size-buffers=50 max-size-bytes=0 max-size-time=0 ! \
        agg1. \
        cropper1. ! \
            queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
            video/x-raw,format=RGB,width=224,height=224,framerate=30/1 ! \
            queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
            hailofilter so-path=$debug_so function-name=$center qos=false ! \
            queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        agg1. \
    agg1. ! queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
    hailooverlay ! \
    queue leaky=downstream max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    videoconvert ! \
    ${sink_pipeline} ${additional_parameters}"

echo "Running $network_name"
echo ${PIPELINE}

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}
