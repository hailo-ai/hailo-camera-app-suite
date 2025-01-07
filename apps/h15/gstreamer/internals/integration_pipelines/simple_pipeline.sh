#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    readonly DEFAULT_VIDEO_SOURCE=""
    readonly DEFAULT_NUMBER_FRAMES="300"
    readonly DEV_VIDEO_PATH="/dev/video"

    readonly RESOURCES_DIR="${CURRENT_DIR}/../../detection/resources"
    readonly POSTPROCESS_DIR="/usr/lib/hailo-post-processes"
    readonly DEFAULT_POSTPROCESS_SO="$POSTPROCESS_DIR/libyolo_post.so"
    readonly DEFAULT_NETWORK_NAME="yolov5"
    readonly DEFAULT_HEF_PATH="${RESOURCES_DIR}/yolov5m_wo_spp_60p_nv12_fhd.hef"
    readonly DEFAULT_JSON_CONFIG_PATH="$RESOURCES_DIR/configs/yolov5.json"
    readonly CROPPING_ALGORITHMS_DIR="$POSTPROCESS_DIR/cropping_algorithms"
    readonly WHOLE_BUFFER_CROP_SO="$CROPPING_ALGORITHMS_DIR/libwhole_buffer.so"
    readonly DEFAULT_OSD_CONFIG="${CURRENT_DIR}/resources/configs/osd_all_res.json"

    postprocess_so=$DEFAULT_POSTPROCESS_SO
    network_name=$DEFAULT_NETWORK_NAME
    input_source=$DEFAULT_VIDEO_SOURCE
    hef_path=$DEFAULT_HEF_PATH
    json_config_path=$DEFAULT_JSON_CONFIG_PATH 
    
    input_source=$DEFAULT_VIDEO_SOURCE
    number_of_frames=$DEFAULT_NUMBER_FRAMES
    hailoosd_config=$DEFAULT_OSD_CONFIG
    hailonet_format="NV12"
    input_caps="format=NV12,width=1920,height=1080,framerate=30/1"
    videoscale_caps="format=NV12,width=1920,height=1080,framerate=30/1"
    encoder_caps="video/x-h265,framerate=30/1"
    queue_size="5"
    hailoupload_pool_size="5"
    hailovideoscale_pool_size="5"
    use_udpsink=false
    rtp_element="videoconvert n-threads=4 ! video/x-raw, format=I420 ! \
    queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! rtpvrawpay"

    print_gst_launch_only=false
    additional_parameters=""

    use_hailoupload=false
    use_hailoupload2=false
    use_hailovideoscale=false
    use_cropper_implementation=false
    use_hailonet=false
    use_hailoosd=false
    use_hailoh265enc=false

    src_pipeline=""
    hailo_upload_pipeline=""
    hailovideoscale_pipeline=""
    hailonet_pipeline=""
    hailoosd_pipeline=""
    hailoh265enc_pipeline=""
    sink_pipeline=""
}

function print_usage() {
    echo "Simple pipeline for hailoupload test"
    echo ""
    echo "Options:"
    echo "  --help                          Show this help"
    echo "  --show-fps                      Print fps"
    echo "  --print-gst-launch              Print the ready gst-launch command without running it"
    echo "  -i INPUT --input INPUT          Set the video source (default $input_source)"
    echo "  --input-caps                    Set the video source input caps before entering the hailoupload"    
    echo "  --number-of-frames              Set the number of frames to pass before sending EOS"    
    echo "  --queue-size                    Set queue size for the whole pipeline"    
    echo "  --use-hailoupload               Use hailoupload on pipeline"    
    echo "  --use-hailoupload2              Use hailoupload2 instead of hailoupload"    
    echo "  --use-hailovideoscale           Use hailovideoscale on pipeline"
    echo "  --videoscale-caps               Set the caps after hailovideoscale implementation"
    echo "  --use-cropper-implementation    Use hailocropper and libgstwholebuffer to implement videoscale"    
    echo "  --use-hailonet                  Use hailonet on pipeline"    
    echo "  --use-hailoosd                  Use hailoosd on pipeline"    
    echo "  --hailoosd-config               Set the json file for osd config"    
    echo "  --use-hailoh265enc              Use hailoh265enc on pipeline"    
    echo "  --encoder-caps                  Set the hailoh265enc caps at output"    
    echo "  --use-udpsink                   Use udp sink instead of fake sink, hardcodded to 10.0.0.2:6543"
    echo "  --use-filesink                  Use file sink instead of fake sink, hardcodded to /media/mmc0_3/temp/output_video"
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
        elif [ "$1" = "--input" ] || [ "$1" = "-i" ]; then
            input_source="$2"
            shift
        elif [ "$1" = "--input-caps" ]; then
            input_caps="$2"
            shift
        elif [ "$1" = "--number-of-frames" ]; then
            number_of_frames="$2"
            shift
        elif [ "$1" = "--queue-size" ]; then
            queue_size="$2"
            shift
        elif [ "$1" = "--use-hailoupload" ]; then
            use_hailoupload=true
        elif [ "$1" = "--use-hailoupload2" ]; then
            use_hailoupload2=true
        elif [ "$1" = "--hailoupload-pool-size" ]; then
            hailoupload_pool_size="$2"
            shift
        elif [ "$1" = "--use-hailovideoscale" ]; then
            use_hailovideoscale=true
        elif [ "$1" = "--videoscale-caps" ]; then
            videoscale_caps="$2"
            shift
        elif [ "$1" = "--hailovideoscale-pool-size" ]; then
            hailovideoscale_pool_size="$2"
            shift
        elif [ "$1" = "--use-cropper-implementation" ]; then
            use_cropper_implementation=true
        elif [ "$1" = "--use-hailonet" ]; then
            use_hailonet=true
        elif [ "$1" = "--use-hailoosd" ]; then
            use_hailoosd=true
        elif [ "$1" = "--hailoosd-config" ]; then
            hailoosd_config="$2"
        elif [ "$1" = "--use-hailoh265enc" ]; then
            use_hailoh265enc=true
        elif [ "$1" = "--encoder-caps" ]; then
            encoder_caps="$2"
            shift
        elif [ "$1" = "--use-udpsink" ]; then
            use_udpsink=true
        elif [ "$1" = "--use-filesink" ]; then
            use_filesink=true
        else
            echo "Received invalid argument: $1. See expected arguments below:"
            print_usage
            exit 1
        fi

        shift
    done
}

function create_src_pipeline() {
    if [[ "$input_source" == *"$DEV_VIDEO_PATH"* ]]; then
        src_pipeline="v4l2src device=$input_source io-mode=dmabuf num-buffers=$number_of_frames ! video/x-raw,$input_caps ! "
    elif [ "$input_source" != "" ]; then
        src_pipeline="filesrc location=$input_source num-buffers=$number_of_frames ! rawvideoparse $input_caps ! "
    else
        src_pipeline="videotestsrc ! video/x-raw,$input_caps ! imagefreeze num-buffers=$number_of_frames ! "
    fi
}

function create_hailoupload_pipeline(){
    if [ "$use_hailoupload" = true ]; then
        hailoupload_pipeline=" \
            queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
            hailoupload pool-size=$hailoupload_pool_size ! \
            queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! "
    fi

    if [ "$use_hailoupload2" = true ]; then
        hailoupload_pipeline=" \
            queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
            hailoupload2 pool-size=$hailoupload_pool_size ! \
            queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! "
    fi
}

function create_hailovideoscale_pipeline(){
    if [ "$use_hailovideoscale" = true ]; then
        if [ "$use_cropper_implementation" = true ]; then
            hailovideoscale_pipeline=" \
                queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
                hailocropper pool-size=$hailovideoscale_pool_size so-path=$WHOLE_BUFFER_CROP_SO function-name=create_crops internal-offset=false name=cropper \
                cropper. ! \
                    queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
                    fakesink \
                cropper. ! \
                    queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
                    video/x-raw, $videoscale_caps ! \
                    queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
                "
        else
            hailovideoscale_pipeline=" \
                queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
                hailovideoscale pool-size=$hailovideoscale_pool_size ! \
                video/x-raw, $videoscale_caps ! "
        fi
    fi
}

function create_hailonet_pipeline(){
    if [ "$use_hailonet" = true ]; then
        hailonet_pipeline=" \
            queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
            hailonet hef-path=$hef_path ! "
    fi
}

function create_hailoosd_pipeline(){
    if [ "$use_hailoosd" = true ]; then
        hailoosd_pipeline=" \
            queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
            hailoosd config-file-path=$hailoosd_config ! "
    fi
}

function create_hailoh265enc_pipeline(){
    if [ "$use_hailoh265enc" = true ]; then
        hailoh265enc_pipeline=" \
            queue leaky=no max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
            hailoh265enc ! \
            $encoder_caps ! "
        rtp_element="rtph265pay ! 'application/x-rtp, media=(string)video, encoding-name=(string)H265' "
    fi
}

function create_sink_pipeline() {
    if [ "$use_udpsink" = true ]; then
        sink_pipeline=" \
            queue max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
            tee name=sink_tee \
            sink_tee. ! \
                queue max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
                fpsdisplaysink fps-update-interval=2000 video-sink=\"fakesink\" sync=true text-overlay=false name=hailo_display \
            sink_tee. ! \
                queue max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
                $rtp_element ! udpsink host=10.0.0.2 port=6543 sync=true"
    elif [ "$use_filesink" = true ]; then
        sink_pipeline=" \
            queue max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
            fpsdisplaysink fps-update-interval=2000 video-sink=\"filesink location=/media/mmc0_3/temp/output_video\" sync=true text-overlay=false name=hailo_display"
    else
        sink_pipeline=" \
            queue max-size-buffers=$queue_size max-size-bytes=0 max-size-time=0 ! \
            fpsdisplaysink fps-update-interval=2000 video-sink=\"fakesink\" sync=true text-overlay=false name=hailo_display"
    fi
}

init_variables $@
parse_args $@

create_src_pipeline
create_hailoupload_pipeline
create_hailovideoscale_pipeline
create_hailonet_pipeline
create_hailoosd_pipeline
create_hailoh265enc_pipeline
create_sink_pipeline

PIPELINE="gst-launch-1.0 \
            ${src_pipeline} \
            ${hailoupload_pipeline} \
            ${hailovideoscale_pipeline} \
            ${hailonet_pipeline} \
            ${hailoosd_pipeline} \
            ${hailoh265enc_pipeline} \
            ${sink_pipeline} ${additional_parameters}"


echo "Running simple pipeline hailoupload"
echo ${PIPELINE}

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}
