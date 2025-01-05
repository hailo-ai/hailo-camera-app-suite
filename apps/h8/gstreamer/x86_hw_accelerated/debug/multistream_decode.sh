#!/bin/bash
set -e

function init_variables() {
    print_help_if_needed $@
    script_dir=$(dirname $(realpath "$0"))
    source $script_dir/../../../../../scripts/misc/checks_before_run.sh --check-vaapi --no-hailo 

    readonly RESOURCES_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/x86_hw_accelerated/dell"

    num_of_src=1
    sink_element="fakesink"
    format="NV12"
    resolution="FHD"
    scaler=""
    additional_parameters=""
    sources=""
    compositor_locations="sink_0::xpos=0 sink_0::ypos=0 sink_1::xpos=640 sink_1::ypos=0 sink_2::xpos=0 sink_2::ypos=640 sink_3::xpos=640 sink_3::ypos=640 sink_4::xpos=1280 sink_4::ypos=0 sink_5::xpos=1280 sink_5::ypos=640 sink_6::xpos=1920 sink_6::ypos=0 sink_7::xpos=1920 sink_7::ypos=640"
    print_gst_launch_only=false
    video_sink_element=$([ "$XV_SUPPORTED" = "true" ] && echo "xvimagesink" || echo "ximagesink")
    json_config_path=$DEFAULT_JSON_CONFIG_PATH         
}

function print_usage() {
    echo "Multistream Detection hailo - pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help                          Show this help"
    echo "  --show-fps                      Printing fps"
    echo "  --format                        Output format"
    echo "  --resolution                    Input Resolution"
    echo "  --add-scale                     Add scale to 640X640"

    echo "  --num-of-sources NUM            Setting number of sources to given input (default value is 4, maximum value is 8)"
    echo "  --print-gst-launch              Print the ready gst-launch command without running it"
    exit 0
}

function print_help_if_needed() {
    while test $# -gt 0; do
        if [ "$1" = "--help" ] || [ "$1" == "-h" ]; then
            print_usage
        fi

        shift
    done
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
            additional_parameters="-v | grep hailo_display | grep dropped"
        elif [ "$1" = "--num-of-sources" ]; then
            shift
            num_of_src=$1
        elif [ "$1" = "--format" ]; then
            shift
            format=$1
        elif [ "$1" = "--sink" ]; then
            shift
            sink_element=$1
        elif [ "$1" = "--add-scale" ]; then
            scaler=" videoscale qos=false n-threads=7 ! video/x-raw, width=640, height=640 ! "
        elif [ "$1" = "--resolution" ]; then
            shift
            resolution=$1
        else
            echo "Received invalid argument: $1. See expected arguments below:"
            print_usage
            exit 1
        fi
        shift
    done
}

function create_sources() {
    start_index=0
    for ((n = $start_index; n < $num_of_src; n++)); do
        sources+="filesrc location=$RESOURCES_DIR/video$n.mp4 name=source_$n ! \
                queue name=pre_decode_q_$n leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
                qtdemux ! vaapidecodebin ! video/x-raw,format=$format,width=1920,height=1080 ! \
		queue name=pre_scale_q_$n leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
		$scaler \
                queue name=hailo_preprocess_q_$n leaky=no max-size-buffers=500 max-size-bytes=0 max-size-time=0 ! \
                fun.sink_$n "
        
    done
}

function main() {
    init_variables $@
    parse_args $@
    create_sources

    pipeline="gst-launch-1.0 \
           funnel name=fun ! \
           queue name=hailo_pre_infer_q_0 leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
	   videoconvert n-threads=5 ! \
           queue name=hailo_pre_display_q_0 leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
           fpsdisplaysink name=hailo_display video-sink=$sink_element sync=false signal-fps-measurements=true text-overlay=false $sources ${additional_parameters}"

    if [ "$print_gst_launch_only" = true ]; then
        echo ${pipeline}
        exit 0
    fi

    fps=`eval "${pipeline}" | tail -n1 | awk '{print $NF}'`
    echo "Pipeline: $resolution - $format - $num_of_src - $fps - $sink_element"

}

main $@
