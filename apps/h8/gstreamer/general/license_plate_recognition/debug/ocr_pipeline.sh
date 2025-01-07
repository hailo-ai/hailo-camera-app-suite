#!/bin/bash
set -e

readonly POSTPROCESS_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/libs/post_processes"
readonly RESOURCES_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/license_plate_recognition/resources"
readonly DEBUG_RESOURCES_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/license_plate_recognition/debug/resources"

readonly LP_DETECTION_HEF_PATH="$RESOURCES_DIR/lprnet.hef"

readonly OCR_POSTPROCESS_SO="$POSTPROCESS_DIR/libocr_post.so"

readonly DEFAULT_PIC_SOURCE="$DEBUG_RESOURCES_DIR/ayalon_license_plate.jpg"

lp_hef=$LP_DETECTION_HEF_PATH
ocr_post=$OCR_POSTPROCESS_SO

input_source=$DEFAULT_PIC_SOURCE

print_gst_launch_only=false
additional_parameters=""

function print_usage() {
    echo "Detection pipeline usage:"
    echo ""
    echo "Options:"
    echo "  -h --help                  Show this help"
    echo "  -i INPUT --input INPUT     Set the input source (default $input_source)"
    echo "  --show-fps                 Print fps"
    echo "  --print-gst-launch         Print the ready gst-launch command without running it"
    echo "  --print-device-stats       Print the power and temperature measured"
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
            additional_parameters="-v | grep -e hailo_display -e hailodevicestats"
        elif [ "$1" = "--input" ] || [ "$1" == "-i" ]; then
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

# If the video provided is from a camera
if [[ $input_source =~ "/dev/video" ]]; then
    source_element="v4l2src device=$input_source name=src_0 ! videoflip video-direction=horiz"
    internal_offset=false
else
    source_element="filesrc location=$input_source name=src_0 ! decodebin"
    internal_offset=true
fi


function main() {
    parse_args $@

    pipeline="gst-launch-1.0 \
        ${source_element} ! videoscale ! videoconvert ! video/x-raw, pixel-aspect-ratio=1/1,format=RGB ! \
        queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailonet hef-path=$lp_hef ! \
        queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailofilter so-path=$ocr_post qos=false ! \
        queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailooverlay line-thickness=2 ! videoconvert ! \
        jpegenc ! filesink location=${TAPPAS_WORKSPACE}/ocr_pipeline.jpg ${additional_parameters}"

    echo ${pipeline}
    if [ "$print_gst_launch_only" = true ]; then
      exit 0
    fi

    echo "Running Pipeline..."
    eval "${pipeline}"

}

main $@
