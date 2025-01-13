#!/bin/bash   
set -m
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    # Basic Directories
    readonly RESOURCES_DIR="${CURRENT_DIR}/resources"

    # Default Video
    readonly DEFAULT_VIDEO_SOURCE="/dev/video0"
    readonly DEFAULT_FRAMERATE="30/1"

    # Config
    readonly DEFAULT_DENOISE_CONFIG_FILE_PATH="$RESOURCES_DIR/configs/denoise_config.json"

    input_source=$DEFAULT_VIDEO_SOURCE
    sync_pipeline=true
    framerate=$DEFAULT_FRAMERATE
    denoise_config_file_path="$DEFAULT_DENOISE_CONFIG_FILE_PATH"

    print_gst_launch_only=false
    additional_parameters=""
    max_buffers_size=5
    skip_frames=100
    num_buffers=220
    averaging_time=10
    output_path="/tmp/images_output/capture_denoise"
    sink="multifilesink location=${output_path}/%05d.raw"

    subdev_number=0
    gain=150
    exposure=1000
}

function print_usage() {
    echo "Hailo15 Capture Denoising pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help                       Show this help"
    echo "  -i INPUT --input INPUT       Set the camera source (default $input_source)"
    echo "  --num-buffers NUM_BUFFERS    Set the num buffers (default $num_buffers)"
    echo "  --skip-frames NUM_FRAMES     Set the num of frames to skip (default $skip_frames)"
    echo "  --no-save         	   Dont save the buffers to disk"
    echo "  --averaging-time NUM_SECONDS Set the num of seconds to run averaging (default $averaging_time)"
    echo "  --show-fps                   Print fps"
    echo "  --print-gst-launch           Print the ready gst-launch command without running it"
    echo "  --denoise-config-file-path   Set the denoise config file path (default $DEFAULT_DENOISE_CONFIG_FILE_PATH)"
    echo "  -g INPUT --gain INPUT        Set the gain (default $gain)"
    echo "  -e INPUT --exposure INPUT    Set the exposure (default $exposure)"
    echo "  -p OUTPUT --output-path OUTPUT  Set the output folder path (default $output_path)"
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
        elif [ "$1" = "--no-save" ]; then
            echo "Printing fps"
            sink="fakesink"
        elif [ "$1" = "--input" ] || [ "$1" = "-i" ]; then
            input_source="$2"
            shift
        elif [ "$1" = "--num-buffers" ]; then
            num_buffers="$2"
            shift
        elif [ "$1" = "--skip-frames" ]; then
            skip_frames="$2"
            shift
        elif [ "$1" = "--averaging-time" ]; then
            averaging_time="$2"
            shift
        elif [ "$1" = "--denoise-config-file-path" ]; then
            denoise_config_file_path="$2"
            shift
        elif [ "$1" = "--gain" ] || [ "$1" = "-g" ]; then
            gain="$2"
            shift
        elif [ "$1" = "--exposure" ] || [ "$1" = "-e" ]; then
            exposure="$2"
            shift
        elif [ "$1" = "--output-path" ] || [ "$1" = "-p" ]; then
            output_path="$2"
            sink="multifilesink location=${output_path}/%05d.raw"
            shift
        else
            echo "Received invalid argument: $1. See expected arguments below:"
            print_usage
            exit 1
        fi

        shift
    done
}

function find_subdev_name()
{
    for i in {0..4}; do
        result=$(cat /sys/class/video4linux/v4l-subdev${i}/name)

        # Check if result contains a substring
        if [[ $result == *"imx334"* || $result == *"imx678"* ]]; then
            echo "Found matching sensor: $result at subdev${i}"
            subdev_number=$i
            break
        fi
    done
}

function set_gain_exposure()
{
    v4l2-ctl --set-ctrl analogue_gain=0 -d /dev/v4l-subdev${subdev_number}
    v4l2-ctl --set-ctrl exposure=0 -d /dev/v4l-subdev${subdev_number}
    v4l2-ctl --set-ctrl analogue_gain=${gain} -d /dev/v4l-subdev${subdev_number}
    v4l2-ctl --set-ctrl exposure=${exposure} -d /dev/v4l-subdev${subdev_number}
    #v4l2-ctl -d /dev/video0 -c isp_ae_integration_time=1002
    #v4l2-ctl -d /dev/video0 -c isp_ae_gain=152
    echo "Finished v4l2-ctl commands, set gain to ${gain} and exposure to ${exposure}"
}

init_variables $@
parse_args $@
find_subdev_name $@

PIPELINE="gst-launch-1.0 \
    v4l2src device=$input_source io-mode=dmabuf num-buffers=$num_buffers ! video/x-raw,format=NV12,width=3840,height=2160,framerate=$framerate ! \
    hailonvalve nframes=$skip_frames ! \
    video/x-raw,format=NV12,width=3840,height=2160,framerate=$framerate ! \
    queue leaky=no max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! \
    hailodenoise config-file-path=$denoise_config_file_path name=denoise ! \
    video/x-raw, width=3840, height=2160, framerate=$framerate, format=NV12 ! \
    queue leaky=no max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! \
    ${sink} \
    ${additional_parameters}"


rm -rf ${output_path}
mkdir -p ${output_path}

echo "Running Pipeline..."
echo ${PIPELINE}
if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE} &
pipeline_pid=$$
echo "Pipeline pid: $pipeline_pid"
sleep 2
set_gain_exposure $@
fg 

