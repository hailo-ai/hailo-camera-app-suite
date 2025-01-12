#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    print_help_if_needed $@

    # Basic Directories
    readonly RESOURCES_DIR="${CURRENT_DIR}/resources"

    # Default Video
    readonly DEFAULT_VIDEO_SOURCE="/dev/video0"

    readonly DEFAULT_MAX_BUFFER_SIZE=5
    readonly DEFAULT_FORMAT="NV12"
    readonly DEFAULT_FRONTEND_CONFIG_FILE_PATH="$RESOURCES_DIR/configs/frontend_config_various.json"

    input_source=$DEFAULT_VIDEO_SOURCE

    
    frontend_config_file_path="$DEFAULT_FRONTEND_CONFIG_FILE_PATH"
    max_buffers_size=$DEFAULT_MAX_BUFFER_SIZE

    print_gst_launch_only=false
    additonal_parameters=""
    video_format=$DEFAULT_FORMAT
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
    echo "Basic security camera streaming pipeline usage:"
    echo ""
    echo "Options:"
    echo "  -h --help                  Show this help"
    echo "  --show-fps                 Print fps"
    echo "  --print-gst-launch         Print the ready gst-launch command without running it"
    echo "  -i --input-source          Set the input source (default $DEFAULT_VIDEO_SOURCE)"
    exit 0
}

function parse_args() {
    while test $# -gt 0; do
        if [ "$1" = "--print-gst-launch" ]; then
            print_gst_launch_only=true
        elif [ "$1" = "--show-fps" ]; then
            echo "Printing fps"
            additonal_parameters="-v | grep -e hailo_display"
        elif [ "$1" = "-i" ] || [ "$1" = "--input-source" ]; then
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

function create_pipeline() {

    FPS_DISP="fpsdisplaysink fps-update-interval=2000 text-overlay=false sync=$sync_pipeline video-sink=fakesink"

    FOUR_K_FIRST_BRANCH="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                         $FPS_DISP name=hailo_display_4k_first "

    FOUR_K_SECOND_BRANCH="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                          $FPS_DISP name=hailo_display_4k_second "

    FHD_BRANCH="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                $FPS_DISP name=hailo_display_fhd "

    HD_BRANCH="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
               $FPS_DISP name=hailo_display_hd "

    SD_FIRST_BRANCH="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                     $FPS_DISP name=hailo_display_sd_first "

    SD_SECOND_BRANCH="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                      $FPS_DISP name=hailo_display_sd_second "

}

create_pipeline $@

PIPELINE="${debug_stats_export} gst-launch-1.0 \
    hailofrontendbinsrc config-file-path=$frontend_config_file_path name=preproc \
    preproc. ! $FOUR_K_FIRST_BRANCH \
    preproc. ! $FOUR_K_SECOND_BRANCH \
    preproc. ! $FHD_BRANCH \
    preproc. ! $HD_BRANCH \
    preproc. ! $SD_FIRST_BRANCH \
    preproc. ! $SD_SECOND_BRANCH \
    ${additonal_parameters} "

echo "Running Pipeline..."
echo ${PIPELINE}

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}