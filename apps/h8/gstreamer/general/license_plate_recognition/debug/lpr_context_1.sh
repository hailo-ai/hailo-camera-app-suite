#!/bin/bash
set -e

readonly POSTPROCESS_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/libs/post_processes"
readonly RESOURCES_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/license_plate_recognition/resources"
readonly DEBUG_RESOURCES_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/license_plate_recognition/debug/resources"
readonly CROPING_ALGORITHMS_DIR="$POSTPROCESS_DIR/cropping_algorithms"
readonly DEFAULT_LICENSE_PLATE_JSON_CONFIG_PATH="$RESOURCES_DIR/configs/yolov4_license_plate.json"

readonly LICENSE_PLATE_CROP_SO="$CROPING_ALGORITHMS_DIR/liblpr_croppers.so"
readonly UNTRACKED_VEHICLE_CROP_FUNCTION="vehicles_without_ocr"

readonly DEFAULT_PIC_SOURCE="$DEBUG_RESOURCES_DIR/ayalon.png"

readonly DEBUG_SO="$POSTPROCESS_DIR/libdebug.so"
readonly DEBUG_FUNCTION="generate_center_detection"

readonly LP_DETECTION_HEF_PATH="$RESOURCES_DIR/tiny_yolov4_license_plates.hef"
readonly OCR_HEF_PATH="$RESOURCES_DIR/lprnet.hef"
readonly LP_DETECTION_POSTPROCESS_SO="$POSTPROCESS_DIR/libyolo_post.so"
readonly LP_DETECTION_NETWORK_NAME="tiny_yolov4_license_plates"

readonly OCR_POSTPROCESS_SO="$POSTPROCESS_DIR/libocr_post.so"
readonly OCR_CROP_FUNCTION="license_plate_quality_estimation"

lp_detection_postprocess_so=$LP_DETECTION_POSTPROCESS_SO

crop_so=$LICENSE_PLATE_CROP_SO
license_plate_detection_crop_func=$UNTRACKED_VEHICLE_CROP_FUNCTION

lp_hef=$LP_DETECTION_HEF_PATH
license_plate_json_config_path=$DEFAULT_LICENSE_PLATE_JSON_CONFIG_PATH
lp_post_name=$LP_DETECTION_NETWORK_NAME
ocr_hef=$OCR_HEF_PATH
ocr_post=$OCR_POSTPROCESS_SO
ocr_crop_func=$OCR_CROP_FUNCTION

input_source=$DEFAULT_PIC_SOURCE

internal_offset=false
print_gst_launch_only=false
additional_parameters=""
lp_detection=""
ocr_pipeline=""

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
    source_element="multifilesrc loop=true location=$input_source name=src_0 ! decodebin"
    internal_offset=true
fi

function create_lp_detection_pipeline() {
    lp_detection="hailocropper so-path=$crop_so function-name=$license_plate_detection_crop_func internal-offset=$internal_offset drop-uncropped-buffers=false resize-method=bilinear name=cropper1 \
                hailoaggregator name=agg1 \
                cropper1. ! \
                    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
                agg1. \
                cropper1. ! \
                    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
                    hailonet hef-path=$lp_hef vdevice-key=1 scheduling-algorithm=1 ! \
                    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
                    hailofilter function-name=$lp_post_name so-path=$lp_detection_postprocess_so config-path=$license_plate_json_config_path qos=false ! \
                    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
                agg1. \
                agg1. ! queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! "
}

function create_ocr_pipeline() {
    ocr_pipeline="hailocropper so-path=$crop_so function-name=$ocr_crop_func internal-offset=$internal_offset drop-uncropped-buffers=false resize-method=bilinear name=cropper2 \
                hailoaggregator name=agg2 flatten-detections=false \
                cropper2. ! \
                    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
                agg2. \
                cropper2. ! \
                    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
                    hailonet hef-path=$ocr_hef vdevice-key=1 scheduling-algorithm=1 ! \
                    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
                    hailofilter so-path=$ocr_post qos=false ! \
                    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
                agg2. \
                agg2. ! queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! "
}

function main() {
    parse_args $@
    create_lp_detection_pipeline
    create_ocr_pipeline

    pipeline="gst-launch-1.0 \
        ${source_element} ! videoscale ! videoconvert ! video/x-raw, pixel-aspect-ratio=1/1,format=RGB ! \
        queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailofilter function-name=$DEBUG_FUNCTION so-path=$DEBUG_SO qos=false ! \
        queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        $lp_detection \
        $ocr_pipeline \
        hailooverlay line_thickness=2 ! videoconvert ! \
        autovideosink "

    echo ${pipeline}
    if [ "$print_gst_launch_only" = true ]; then
      exit 0
    fi

    echo "Running Pipeline..."
    eval "${pipeline}"
}

main $@
