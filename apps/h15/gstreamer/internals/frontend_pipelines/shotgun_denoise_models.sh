#!/bin/bash
set -m
set -e
  
CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    # Basic Directories
    readonly RESOURCES_DIR="${CURRENT_DIR}/resources"
    readonly CONFIG_DIR="${RESOURCES_DIR}/configs"

    # Default Video
    readonly DEFAULT_VIDEO_SOURCE="/dev/video0"
    readonly DEFAULT_FRAMERATE="30/1"

    # Config# Default values
    denoise_config_files=(  "vd_s.json"
                            "vd_m.json"
                            "vd_l.json")

    input_source=$DEFAULT_VIDEO_SOURCE
    sync_pipeline=true
    framerate=$DEFAULT_FRAMERATE
    denoise_config_file_path="$DEFAULT_DENOISE_CONFIG_FILE_PATH"

    additional_parameters=""
    skip_frames=100
    num_buffers=220
    averaging_time=10

    subdev_number=0
    gain=150
    exposure=1000
}

function print_usage() {
    echo "Hailo15 Shotgun Denoising Models pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help                       Show this help"
    echo "  --num-buffers NUM_BUFFERS    Set the num buffers (default $num_buffers)"
    echo "  --skip-frames NUM_FRAMES     Set the num of frames to skip (default $skip_frames)"
    echo "  --averaging-time NUM_SECONDS Set the num of seconds to run averaging (default $averaging_time)"
    echo "  -g INPUT --gain INPUT        Set the gain (default $gain)"
    echo "  -e INPUT --exposure INPUT    Set the exposure (default $exposure)"
    exit 0
}

function parse_args() {
    while test $# -gt 0; do
        if [ "$1" = "--help" ] || [ "$1" == "-h" ]; then
            print_usage
            exit 0
        elif [ "$1" = "--num-buffers" ]; then
            num_buffers="$2"
            shift
        elif [ "$1" = "--skip-frames" ]; then
            skip_frames="$2"
            shift
        elif [ "$1" = "--averaging-time" ]; then
            averaging_time="$2"
            shift
        elif [ "$1" = "--gain" ] || [ "$1" = "-g" ]; then
            gain="$2"
            shift
        elif [ "$1" = "--exposure" ] || [ "$1" = "-e" ]; then
            exposure="$2"
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

# Run capture_4k_no_avg.sh
echo ""
echo "Running capture_4k_no_avg.sh with --num-buffers $num_buffers --gain $gain --exposure $exposure"
/bin/bash ${CURRENT_DIR}/capture_4k_no_avg.sh --num-buffers "$num_buffers" --gain "$gain" --exposure "$exposure"

# Run capture_denoise_no_avg.sh with different denoise config files
for config_file in "${denoise_config_files[@]}"; do
    model_name=$(basename "$config_file" .json)  # Strip trailing ".json"
    echo ""
    echo "Running capture_denoise_no_avg.sh with --num-buffers $num_buffers --gain $gain --exposure $exposure --denoise-config-file-path ${CONFIG_DIR}/${config_file} --output-path /tmp/images_output/${model_name}"
    /bin/bash ${CURRENT_DIR}/capture_denoise_no_avg.sh --num-buffers "$num_buffers" --gain "$gain" --exposure "$exposure" --denoise-config-file-path "${CONFIG_DIR}/${config_file}" --output-path "/tmp/images_output/${model_name}"
done
