#!/bin/bash
set -e

readonly POSTPROCESS_DIR="/usr/lib/hailo-post-processes"
readonly CROPPING_ALGORITHMS_DIR="$POSTPROCESS_DIR/cropping_algorithms"
readonly WHOLE_BUFFER_CROP_SO="$CROPPING_ALGORITHMS_DIR/libwhole_buffer.so"

MAX_NUM_PARALLEL_VIDEOSCALE=16

PIPELINE="gst-launch-1.0 "

start_index=0
for ((n = $start_index; n < $MAX_NUM_PARALLEL_VIDEOSCALE; n++)); do
    PARTIAL_PIPELINE="\
    videotestsrc ! hailoupload pool-size=2 ! \
    imagefreeze ! video/x-raw,format=NV12,width=1920,height=1080 ! \
    queue leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! \
    hailocropper pool-size=3 so-path=$WHOLE_BUFFER_CROP_SO function-name=create_crops internal-offset=false name=cropper$n \
        cropper$n. ! \
            queue leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! \
            fakesink \
        cropper$n. ! \
            video/x-raw,format=NV12,width=1280,height=720 ! \
            queue leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! \
            fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false sync=false "
    PIPELINE+=$PARTIAL_PIPELINE
done

PIPELINE+=" -v"

echo ${PIPELINE}
eval ${PIPELINE}
