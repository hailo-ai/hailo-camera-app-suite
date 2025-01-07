#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    print_help_if_needed $@

    # Basic Directories
    readonly RESOURCES_DIR="${CURRENT_DIR}/resources"
    readonly DETECTION_RESOURCES_DIR="/home/root/apps/detection/resources"

    # Default Video
    readonly DEFAULT_VIDEO_SOURCE="/dev/video0"

    readonly FOUR_K_BITRATE=10000000

    readonly DEFAULT_MAX_BUFFER_SIZE=1
    readonly DEFAULT_FORMAT="NV12"
    readonly DEFAULT_BITRATE=10000000
    readonly DEFAULT_DENOISE_CONFIG_FILE_PATH="$RESOURCES_DIR/configs/denoise_config.json"
    readonly DEFAULT_HEF_FILE_PATH="$DETECTION_RESOURCES_DIR/yolov5m_wo_spp_60p_nv12_640.hef"

    input_source=$DEFAULT_VIDEO_SOURCE
    denoise_config_file_path="$DEFAULT_DENOISE_CONFIG_FILE_PATH"

    max_buffers_size=$DEFAULT_MAX_BUFFER_SIZE

    print_gst_launch_only=false
    additonal_parameters=""
    video_format=$DEFAULT_FORMAT
    hef_path=$DEFAULT_HEF_FILE_PATH
    sync_pipeline=false
}

function print_help_if_needed() {
    while test $# -gt 0; do
        if [ "$1" = "--help" ] || [ "$1" == "-h" ]; then
            print_usage
        fi

        shift
    done
}

function print_usage() {
    echo "Denoise streaming pipeline usage:"
    echo ""
    echo "Options:"
    echo "  -h --help                  Show this help"
    echo "  --show-fps                 Print fps"
    echo "  --print-gst-launch         Print the ready gst-launch command without running it"
    echo "  -i --input-source          Set the input source (default ${DEFAULT_VIDEO_SOURCE})"
    echo "  --denoise-config-file-path  Set the denoise config file path (default ${DEFAULT_DENOISE_CONFIG_FILE_PATH})"
    exit 0
}

function parse_args() {
    while test $# -gt 0; do
        if [ "$1" = "--print-gst-launch" ]; then
            print_gst_launch_only=true
        elif [ "$1" = "--show-fps" ]; then
            echo "Printing fps"
            additonal_parameters="-v | grep -e hailo_display "
        elif [ "$1" = "-i" ] || [ "$1" = "--input-source" ]; then
            input_source="$2"
            shift
        elif [ "$1" = "--denoise-config-file-path" ]; then
            denoise_config_file_path="$2"
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


PIPELINE="gst-launch-1.0 \
    v4l2src io-mode=dmabuf device=$input_source num-buffers=1 ! video/x-raw, width=3840, height=2160, framerate=30/1, format=$video_format ! \
    imagefreeze ! \
    queue leaky=downstream max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    hailodenoise config-file-path=$denoise_config_file_path name=denoise ! \
    queue leaky=downstream max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    hailovideoscale use-letterbox=true ! \
    video/x-raw,format=NV12,width=640,height=640 ! \
    hailonet batch-size=4 scheduler-threshold=4 scheduler-timeout-ms=1000 hef-path=$hef_path nms-iou-threshold=0.45 nms-score-threshold=0.3 scheduling-algorithm=1 vdevice-group-id=device0 ! \
    queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink name=hailo_display fps-update-interval=2000 text-overlay=false sync=$sync_pipeline video-sink=fakesink \
    ${additonal_parameters} "

echo "Running Pipeline..."
echo ${PIPELINE}

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}
