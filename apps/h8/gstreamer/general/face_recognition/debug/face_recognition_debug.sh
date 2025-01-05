#!/bin/bash
set -e

function init_variables() {
    print_help_if_needed $@
    script_dir=$(dirname $(realpath "$0"))
    source $script_dir/../../../../../scripts/misc/checks_before_run.sh

    readonly RESOURCES_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/face_recognition/resources"
    readonly POSTPROCESS_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/libs/post_processes/"
    readonly APPS_LIBS_DIR="$TAPPAS_WORKSPACE/apps/h8/gstreamer/libs/apps/vms/"
    readonly POSTPROCESS_SO="$POSTPROCESS_DIR/libface_detection_post.so"
    readonly CROPPER_SO="$POSTPROCESS_DIR/cropping_algorithms/libvms_croppers.so"
    readonly FACE_ALIGN_SO="$APPS_LIBS_DIR/libvms_face_align.so"
    readonly RECOGNITION_POST_SO="$POSTPROCESS_DIR/libface_recognition_post.so"
    readonly HEF_PATH="$RESOURCES_DIR/retinaface_mobilenet_v1.hef"
    readonly RECOGNITION_HEF_PATH="$RESOURCES_DIR/arcface_mobilefacenet_v1.hef"
    readonly RE_ID_OVERLAY="$TAPPAS_WORKSPACE/apps/h8/gstreamer/libs/apps/re_id/libre_id_overlay.so"
    readonly FUNCTION_NAME="retinaface"
    readonly WHOLE_BUFFER_CROP_SO="$POSTPROCESS_DIR/cropping_algorithms/libwhole_buffer.so"

    video_sink_element=$([ "$XV_SUPPORTED" = "true" ] && echo "xvimagesink" || echo "ximagesink")
    num_of_src=4
    additional_parameters=""
    sources=""
    camera_input_source="v4l2src device=/dev/video2 ! video/x-raw,format=YUY2,width=1920,height=1080,framerate=30/1 ! videoconvert"
    compositor_locations="sink_0::xpos=0 sink_0::ypos=0 sink_1::xpos=800 sink_1::ypos=0 sink_2::xpos=0 sink_2::ypos=450 sink_3::xpos=800 sink_3::ypos=450"
    print_gst_launch_only=false
    vdevice_key=1
    sync_mode=true
    source_prefix="recognition"
    local_gallery_file="$RESOURCES_DIR/gallery/face_recognition_local_gallery.json"
}

function print_usage() {
    echo "RE-ID app hailo - pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help                          Show this help"
    echo "  --show-fps                      Printing fps"
    echo "  --num-of-sources NUM            Setting number of sources to given input (default value is 4)"
    echo "  --print-gst-launch              Print the ready gst-launch command without running it"
    echo "  --online-dewarp                 Perform online dewarping"
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
            additional_parameters="-v 2>&1 | grep hailo_display"
        elif [ "$1" = "--num-of-sources" ]; then
            shift
            echo "Setting number of sources to $1"
            num_of_src=$1
        else
            echo "Received invalid argument: $1. See expected arguments below:"
            print_usage
            exit 1
        fi
        shift
    done
}

function create_sources() {
    # input_source="filesrc location=$RESOURCES_DIR/rec_belz.png ! decodebin"
    input_source="filesrc location=$RESOURCES_DIR/face_recognition.mp4 ! decodebin"
}

function main() {
    init_variables $@
    parse_args $@
    create_sources

    DUMP_IMAGE_TEE="tee name=test_tee \
        test_tee. ! queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        videoscale qos=false ! \
        queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        videoconvert qos=false ! \
        ximagesink \
        test_tee. "

    DUMP_IMAGE_TO_FILE_TEE="tee name=test_tee \
        test_tee. ! queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        videoscale qos=false ! \
        queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        videoconvert qos=false ! \
        pngenc ! \
        multifilesink location=image.png \
        test_tee. "

    RECOGNITION_PIPELINE="queue name=hailo_pre_cropper2_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailocropper so-path=$CROPPER_SO function-name=face_recognition internal-offset=true name=cropper2 \
        hailoaggregator name=agg2 \
        cropper2. ! queue name=bypess2_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! agg2. \
        cropper2. ! queue name=pre_face_align_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailofilter so-path=$FACE_ALIGN_SO use-gst-buffer=true qos=false ! \
        $DUMP_IMAGE_TEE ! \
        queue name=detector_pos_face_align_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailonet hef-path=$RECOGNITION_HEF_PATH scheduling-algorithm=1 vdevice-key=1 ! \
        queue name=recognition_post_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailofilter so-path=$RECOGNITION_POST_SO qos=false ! \
        queue name=recognition_pre_agg_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        agg2. agg2. "

    DETECTION_PIPELINE="queue name=hailo_pre_cropper1_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailonet hef-path=$HEF_PATH scheduling-algorithm=1 vdevice-key=1 ! \
        queue name=detector_post_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailofilter so-path=$POSTPROCESS_SO qos=false function_name=$FUNCTION_NAME"

    pipeline="gst-launch-1.0 \
        $input_source ! \
        queue name=hailo_pre_convert_0 leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        videoconvert n-threads=3 qos=false ! video/x-raw,format=RGB ! \
        queue name=pre_detector_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        videoscale qos=false ! \
        $DETECTION_PIPELINE ! \
        queue name=pre_tracker_q leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailotracker name=hailo_tracker class-id=-1 kalman-dist-thr=0.7 iou-thr=0.8 init-iou-thr=0.9 \
        keep-new-frames=2 keep-tracked-frames=2 keep-lost-frames=2 keep-past-metadata=false qos=false ! \
        queue name=hailo_pre_tracker leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        $RECOGNITION_PIPELINE ! \
        queue name=hailo_post_tracker leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailogallery gallery-file-path=$local_gallery_file \
        load-local-gallery=true similarity-thr=.4 gallery-queue-size=20 class-id=-1 ! \
        queue name=hailo_pre_draw2 leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        hailooverlay show-confidence=false name=overlay2 qos=false ! \
        queue name=hailo_post_draw leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
        videoconvert n-threads=2 qos=false ! \
        queue name=hailo_display_q_0 leaky=no max_size_buffers=300 max-size-bytes=0 max-size-time=0 ! \
        fpsdisplaysink video-sink=$video_sink_element name=hailo_display sync=false text-overlay=false \
        ${additional_parameters}"

    echo ${pipeline}
    if [ "$print_gst_launch_only" = true ]; then
        exit 0
    fi

    echo "Running Pipeline..."
    eval "${pipeline}"

}

main $@
