#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    print_help_if_needed $@

    # Basic Directories
    readonly POSTPROCESS_DIR="/usr/lib/hailo-post-processes"
    readonly CROPPING_ALGORITHMS_DIR="$POSTPROCESS_DIR/cropping_algorithms"
    readonly RESOURCES_DIR="${CURRENT_DIR}/../../license_plate_recognition/resources"
    readonly DEFAULT_LICENSE_PLATE_JSON_CONFIG_PATH="$RESOURCES_DIR/configs/yolov4_license_plate.json"
    readonly DEFAULT_VEHICLE_JSON_CONFIG_PATH="$RESOURCES_DIR/configs/yolov5_vehicle_detection.json"
    readonly DEBUG_SO="$POSTPROCESS_DIR/libdebug.so"
    readonly DEBUG_FUNCTION="generate_center_detection"

    # Default Video
    readonly DEFAULT_VIDEO_SOURCE="$RESOURCES_DIR/lpr_rgb.raw"

    # Vehicle Detection Macros
    readonly VEHICLE_DETECTION_HEF="$RESOURCES_DIR/small_yolov5m_vehicles.hef"
    readonly VEHICLE_DETECTION_POST_SO="$POSTPROCESS_DIR/libyolo_post.so"
    readonly VEHICLE_DETECTION_POST_FUNC="yolov5_vehicles_only"

    # Cropping Algorithm Macros
    readonly LICENSE_PLATE_CROP_SO="$CROPPING_ALGORITHMS_DIR/liblpr_croppers.so"
    readonly WHOLE_BUFFER_CROP_SO="$CROPPING_ALGORITHMS_DIR/libwhole_buffer.so"

    readonly DEFAULT_SOURCE_TYPE="filesrc"
    readonly DEFAULT_POOL_SIZE=10

    input_source=$DEFAULT_VIDEO_SOURCE

    pool_size=$DEFAULT_POOL_SIZE
    num_buffers=-1
    use_hailoupload=false
    use_infer_pipeline=false
    input_source_type=$DEFAULT_SOURCE_TYPE

    print_gst_launch_only=false
    additonal_parameters=""
    stats_element=""
    debug_stats_export=""
    sync_pipeline=false
    device_id_prop=""
    tee_name="context_tee"
    internal_offset=false
    pipeline_1=""
    video_format="RGB"
    license_plate_json_config_path=$DEFAULT_LICENSE_PLATE_JSON_CONFIG_PATH
    car_json_config_path=$DEFAULT_VEHICLE_JSON_CONFIG_PATH 
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
    echo "IMX8 LPR pipeline usage:"
    echo ""
    echo "Options:"
    echo "  -h --help                  Show this help"
    echo "  --show-fps                 Print fps"
    echo "  --print-gst-launch         Print the ready gst-launch command without running it"
    echo "  --print-device-stats       Print the power and temperature measured"
    echo "  --pool-size                Set pool size for hailocropper elements"
    echo "  --use-hailoupload          Use hailoupload"
    echo "  --infer-pipeline           Use infer pipeline"
    echo "  --source                   Set the source of the pipeline(v4l2src or filesrc)"
    echo "  --num-buffers              Set the number of buffers to process (default -1 for infinite)"
    echo "  -i --input-source          Set the input source (default $DEFAULT_VIDEO_SOURCE)"
    echo " --format                    Set the format of the input source [RGB, NV12] (default RGB)"
    exit 0
}

function parse_args() {
    while test $# -gt 0; do
        if [ "$1" = "--print-gst-launch" ]; then
            print_gst_launch_only=true
        elif [ "$1" = "--print-device-stats" ]; then
            hailo_bus_id=$(hailortcli scan | awk '{ print $NF }' | tail -n 1)
            device_id_prop="device_id=$hailo_bus_id"
            stats_element="hailodevicestats $device_id_prop"
            debug_stats_export="GST_DEBUG=hailodevicestats:5"
        elif [ "$1" = "--pool-size" ]; then
            pool_size=$2
            shift
        elif [ "$1" = "--use-hailoupload" ]; then
            use_hailoupload=true
        elif [ "$1" = "--infer-pipeline" ]; then
            use_infer_pipeline=true
        elif [ "$1" = "--source" ]; then
            input_source_type=$2
            shift
        elif [ "$1" = "--num-buffers" ]; then
            num_buffers=$2
            shift
        elif [ "$1" = "--show-fps" ]; then
            echo "Printing fps"
            additonal_parameters="-v | grep -e hailo_display -e hailodevicestats"
        elif [ "$1" = "-i" ] || [ "$1" = "--input-source" ]; then
            input_source="$2"
            shift
            if [[ "$input_source" == *"/dev/video"* ]]; then
                $input_source_type = "v4l2src"
            fi
        elif [ "$1" = "--format" ]; then
            video_format="$2"
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

if [ "$input_source_type" = "v4l2src" ]; then
    source_element="v4l2src io-mode=dmabuf device=/dev/video0 name=src_0 num-buffers=$num_buffers ! video/x-raw, width=1920, height=1080, format=$video_format"
elif [ "$input_source_type" = "filesrc" ]; then
    video_format_lower=$(echo $video_format | tr '[:upper:]' '[:lower:]')
    source_element="filesrc location=$input_source name=src_0 num-buffers=$num_buffers ! rawvideoparse format=$video_format_lower width=1920 height=1080"
else
    echo "Invalid source: $input_source. Expected v4l2src or filesrc"
    exit 1
fi

internal_offset=true

function create_lp_detection_pipeline() {
    DUMP_IMAGE_TO_FILE_TEE="tee name=test_tee \
        test_tee. ! queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
        videoconvert qos=false ! \
        pngenc ! \
        multifilesink location=image.png \
        test_tee. "

    UDPSINK="videoconvert qos=false n-threads=4 ! video/x-raw, format=I420 ! \
            queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
            rtpvrawpay ! 'application/x-rtp, media=(string)video, encoding-name=(string)RAW' ! udpsink host=10.0.0.2 port=5000 sync=false"

    UDPSINK2="videoscale qos=false n-threads=4 ! video/x-raw, width=300, height=300 ! \
            queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
            videoconvert qos=false n-threads=4 ! video/x-raw, format=I420 ! \
            queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
            rtpvrawpay ! 'application/x-rtp, media=(string)video, encoding-name=(string)RAW' ! udpsink host=10.0.0.2 port=5002 sync=false"

    DUMP_IMAGE_TO_UDP_TEE="tee name=test_tee \
        test_tee. ! queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
        $UDPSINK \
        test_tee. "

    pipeline_3="\
                hailocropper pool-size=$pool_size so-path=$WHOLE_BUFFER_CROP_SO function-name=create_crops internal-offset=$internal_offset name=cropper3 \
                hailoaggregator name=agg3 \
                cropper3. ! \
                    queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
                agg3. \
                cropper3. ! \
                    video/x-raw,format=$video_format, width=300, height=300, pixel-aspect-ratio=1/1 ! \
                    queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
                agg3. \
                agg3."

    hailoupload_pipe="queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0"
    if [ "$use_hailoupload" = true ]; then
        hailoupload_pipe="queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
                        hailoupload pool-size=$pool_size ! \
                        queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0"
    fi

    pipeline_2="\
                hailocropper pool-size=$pool_size so-path=$WHOLE_BUFFER_CROP_SO function-name=create_crops internal-offset=$internal_offset name=cropper2 \
                hailoaggregator name=agg2 \
                cropper2. ! \
                    queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
                agg2. \
                cropper2. ! \
                    video/x-raw,format=$video_format, width=120, height=120, pixel-aspect-ratio=1/1 ! \
                    queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
                agg2. \
                agg2."

    infer_pipeline="\
                hailocropper pool-size=$pool_size so-path=$WHOLE_BUFFER_CROP_SO function-name=create_crops internal-offset=$internal_offset name=cropper1 \
                hailoaggregator name=agg1 \
                cropper1. ! \
                    queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
                agg1. \
                cropper1. ! \
                    video/x-raw,format=$video_format, width=640, height=640, pixel-aspect-ratio=1/1 ! \
                    queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
                agg1. \
                agg1."


    if [ "$use_infer_pipeline" = true ]; then
        infer_pipeline="\
                    hailocropper pool-size=$pool_size so-path=$WHOLE_BUFFER_CROP_SO function-name=create_crops internal-offset=$internal_offset drop-uncropped-buffers=true name=infer_cropper \
                    hailoaggregator name=infer_agg \
                    infer_cropper. ! \
                        queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
                    infer_agg. \
                    infer_cropper. ! \
                        queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
                        hailonet hef-path=$VEHICLE_DETECTION_HEF vdevice-key=1 scheduling-algorithm=1 ! \
                        queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
                        hailofilter so-path=$VEHICLE_DETECTION_POST_SO config-path=$car_json_config_path function-name=$VEHICLE_DETECTION_POST_FUNC qos=false ! \
                        queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
                    infer_agg. \
                    infer_agg."
    fi
}
create_lp_detection_pipeline $@

FPS_DISP="fpsdisplaysink fps-update-interval=2000 name=hailo_display text-overlay=false sync=false video-sink=fakesink"

PIPELINE="${debug_stats_export} gst-launch-1.0 ${stats_element} \
    $source_element ! \
    $hailoupload_pipe ! \
    $infer_pipeline ! \
    queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
    $pipeline_2 ! \
    queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
    $pipeline_3 ! \
    queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
    hailooverlay ! \
    queue leaky=no max-size-buffers=10 max-size-bytes=0 max-size-time=0 ! \
    $FPS_DISP ${additonal_parameters}"

echo "Running License Plate Recognition"
echo ${PIPELINE}

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}