#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    print_help_if_needed $@

    # Basic Directories
    readonly RESOURCES_DIR="${CURRENT_DIR}/resources"

    # Default Video
    readonly DEFAULT_VIDEO_SOURCE="/dev/video0"

    readonly FOUR_K_BITRATE=10000000

    readonly DEFAULT_MAX_BUFFER_SIZE=1
    readonly DEFAULT_FORMAT="NV12"
    readonly DEFAULT_BITRATE=10000000
    readonly DEFAULT_DENOISE_CONFIG_FILE_PATH="$RESOURCES_DIR/configs/denoise_config.json"

    input_source=$DEFAULT_VIDEO_SOURCE
    denoise_config_file_path="$DEFAULT_DENOISE_CONFIG_FILE_PATH"

    encoding_hrd="hrd=false"

    # Limit the encoding bitrate to 10Mbps to support weak host.
    # Comment this out if you encounter a large latency in the host side
    # Tune the value down to reach the desired latency (will decrease the video quality).
    # ----------------------------------------------
    # bitrate=10000000
    # encoding_hrd="hrd=true hrd-cpb-size=$bitrate"
    # ----------------------------------------------

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
            additonal_parameters="-v | grep -e hailo_display"
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

function create_pipeline() {

    FPS_DISP="fpsdisplaysink fps-update-interval=2000 text-overlay=false sync=$sync_pipeline video-sink=fakesink"

    UDP_SINK="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
              rtph264pay ! 'application/x-rtp, media=(string)video, encoding-name=(string)H264' ! \
              udpsink host=10.0.0.2 sync=$sync_pipeline"

    FOUR_K_TO_ENCODER_BRANCH="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                            video/x-raw, width=3840, height=2160, framerate=30/1, format=$video_format ! \
                            hailoh264enc bitrate=$FOUR_K_BITRATE hrd=false ! \
                            video/x-h264 ! \
                            tee name=fourk_enc_tee \
                            fourk_enc_tee. ! \
                                $UDP_SINK port=5000 \
                            fourk_enc_tee. ! \
                                queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                                $FPS_DISP name=hailo_display "
}

create_pipeline $@

PIPELINE="${debug_stats_export} gst-launch-1.0 \
    v4l2src io-mode=dmabuf device=$input_source ! video/x-raw, width=3840, height=2160, framerate=30/1, format=$video_format ! \
    queue leaky=downstream max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    hailodenoise config-file-path=$denoise_config_file_path name=denoise ! \
    $FOUR_K_TO_ENCODER_BRANCH \
    ${additonal_parameters} "

echo "Running Pipeline..."
echo ${PIPELINE}

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}
