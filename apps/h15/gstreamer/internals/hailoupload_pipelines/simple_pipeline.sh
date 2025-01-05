#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    readonly DEFAULT_VIDEO_SOURCE="videotestsrc"
    readonly DEFAULT_NUMBER_FRAMES="300"
    readonly DEV_VIDEO_PATH="/dev/video"

    readonly RESOURCES_DIR="${CURRENT_DIR}/../../detection/resources"
    readonly POSTPROCESS_DIR="/usr/lib/hailo-post-processes"
    readonly DEFAULT_POSTPROCESS_SO="$POSTPROCESS_DIR/libyolo_post.so"
    readonly DEFAULT_NETWORK_NAME="yolov5"
    readonly DEFAULT_HEF_PATH="${RESOURCES_DIR}/yolov5m_wo_spp_60p_nv12_fhd.hef"
    readonly DEFAULT_JSON_CONFIG_PATH="$RESOURCES_DIR/configs/yolov5.json" 

    postprocess_so=$DEFAULT_POSTPROCESS_SO
    network_name=$DEFAULT_NETWORK_NAME
    input_source=$DEFAULT_VIDEO_SOURCE
    hef_path=$DEFAULT_HEF_PATH
    json_config_path=$DEFAULT_JSON_CONFIG_PATH 
    
    input_source=$DEFAULT_VIDEO_SOURCE
    number_of_frames=$DEFAULT_NUMBER_FRAMES
    hailonet_format="NV12"
    input_caps="format=NV12,width=1920,height=1080"
    pre_queue_size="5"
    pool_size="2"
    post_queue_size="5"
    use_udpsink=false
    rtp_element="rtpvrawpay"
    post_pipeline_type=""

    print_gst_launch_only=false
    additional_parameters=""

    src_pipeline=""
    hailo_upload_element=""
    post_upload_pipeline=""
    sink_pipeline=""
}

function print_usage() {
    echo "Simple pipeline for hailoupload test"
    echo ""
    echo "Options:"
    echo "  --help                  Show this help"
    echo "  --show-fps              Print fps"
    echo "  --print-gst-launch      Print the ready gst-launch command without running it"
    echo "  -i INPUT --input INPUT  Set the video source (default $input_source)"
    echo "  --input-caps            Set the video source input caps before entering the hailoupload"    
    echo "  --number-of-frames      Set the number of frames to pass before sending EOS"    
    echo "  --pre-queue-size        Set the queue size before hailoupload"
    echo "  --post-queue-size       Set the queue size after hailo upload"
    echo "  --pool-size             Set the hailo upload size"
    echo "  --post-pipeline-type    Set a hardcodded pipeline after the hailoupload (the options are "
    echo "                          hailonet, hailooverlay, identity, hailoh265enc, hailoh264enc, hailovideoscale, hailoosd) "
    echo "  --use-udpsink           Use udp sink instead of fake sink, hardcodded to 10.0.0.2:6543"
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
        elif [ "$1" = "--pre-queue-size" ]; then
            pre_queue_size="$2"
            shift
        elif [ "$1" = "--post-queue-size" ]; then
            post_queue_size="$2"
            shift
        elif [ "$1" = "--pool-size" ]; then
            pool_size="$2"
            shift
        elif [ "$1" = "--post-pipeline-type" ]; then
            post_pipeline_type="$2"
            shift
        elif [ "$1" = "--use-udpsink" ]; then
            use_udpsink=true
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
        src_pipeline="v4l2src device=$input_source io-mode=dmabuf num-buffers=$number_of_frames ! video/x-raw,$input_caps"
    else
        src_pipeline="videotestsrc num-buffers=$number_of_frames ! video/x-raw,$input_caps"
    fi
}

function create_hailo_upload_element(){
    hailo_upload_element="hailoupload pool-size=$pool_size"
}

function create_post_upload_pipeline(){
    if [ "$post_pipeline_type" = "" ]; then
        post_upload_pipeline=""
    elif [ "$post_pipeline_type" = "hailonet" ]; then
        post_upload_pipeline=" \
            hailonet hef-path=$hef_path ! \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! \
            hailofilter function-name=$network_name config-path=$json_config_path so-path=$postprocess_so qos=false ! \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
        if [[ "$input_caps" == *"$hailonet_format"* && "$use_udpsink" = true ]]; then
            post_upload_pipeline+="\
                queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! \
                hailoh265enc ! \
                video/x-h265,framerate=30/1 ! \
                queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
            rtp_element="rtph265pay ! 'application/x-rtp, media=(string)video, encoding-name=(string)H265' "
        fi
    elif [ "$post_pipeline_type" = "hailooverlay" ]; then
        post_upload_pipeline=" \
            hailonet hef-path=$hef_path ! \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! \
            hailofilter function-name=$network_name config-path=$json_config_path so-path=$postprocess_so qos=false ! \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! \
            hailooverlay qos=false ! \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
        if [[ "$input_caps" == *"$hailonet_format"* && "$use_udpsink" = true ]]; then
            post_upload_pipeline+="\
                queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! \
                hailoh265enc ! \
                video/x-h265,framerate=30/1 ! \
                queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
            rtp_element="rtph265pay ! 'application/x-rtp, media=(string)video, encoding-name=(string)H265' "
        fi
    elif [ "$post_pipeline_type" = "identity" ]; then
        post_upload_pipeline=" \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! \
            identity sleep-time=1000000 qos=false ! \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
    elif [ "$post_pipeline_type" = "hailoh265enc" ]; then
        post_upload_pipeline=" \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! \
            hailoh265enc ! \
            video/x-h265,framerate=30/1 ! \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
        rtp_element="rtph265pay ! 'application/x-rtp, media=(string)video, encoding-name=(string)H265' "
    elif [ "$post_pipeline_type" = "hailoh264enc" ]; then
        post_upload_pipeline=" \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! \
            hailoh264enc ! \
            video/x-h264,framerate=30/1 ! \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
        rtp_element="rtph264pay ! 'application/x-rtp, media=(string)video, encoding-name=(string)H264' "
    elif [ "$post_pipeline_type" = "hailovideoscale" ]; then
        post_upload_pipeline=" \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! \
            hailovideoscale ! video/x-raw, width=640, height=640 ! \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
        if [ "$use_udpsink" = true ]; then
            post_upload_pipeline+="\
                hailoh265enc ! \
                video/x-h265,framerate=30/1 ! \
                queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
            rtp_element="rtph265pay ! 'application/x-rtp, media=(string)video, encoding-name=(string)H265' "
        fi
    elif [ "$post_pipeline_type" = "hailoosd" ]; then
        post_upload_pipeline=" \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! \
            hailoosd ! \
            queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
        if [ "$use_udpsink" = true ]; then
            post_upload_pipeline+="\
                hailoh265enc ! \
                video/x-h265,framerate=30/1 ! \
                queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
            rtp_element="rtph265pay ! 'application/x-rtp, media=(string)video, encoding-name=(string)H265' "
        fi
    else
            echo "Received invalid type: $post_pipeline_type."
            exit 1
    fi
}

function create_sink_pipeline() {
    if [ "$use_udpsink" = true ]; then
        sink_pipeline="tee name=sink_tee \
        sink_tee. ! \
            queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! \
            fpsdisplaysink fps-update-interval=2000 video-sink=\"fakesink\" sync=false text-overlay=false name=hailo_display \
        sink_tee. ! \
            queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! \
            $rtp_element ! udpsink host=10.0.0.2 port=6543 sync=false"
    else
        sink_pipeline="fpsdisplaysink fps-update-interval=2000 video-sink=\"fakesink\" sync=false text-overlay=false name=hailo_display"
    fi
}

init_variables $@
parse_args $@

create_src_pipeline
create_hailo_upload_element
create_post_upload_pipeline
create_sink_pipeline

PIPELINE="gst-launch-1.0 \
            ${src_pipeline} ! \
            queue leaky=no max-size-buffers=$pre_queue_size max-size-bytes=0 max-size-time=0 ! \
            ${hailo_upload_element} ! \
            queue leaky=no max-size-buffers=$post_queue_size max-size-bytes=0 max-size-time=0 ! \
            ${post_upload_pipeline} \
            ${sink_pipeline} ${additional_parameters}"

echo "Running simple pipeline hailoupload"
echo ${PIPELINE}

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}
