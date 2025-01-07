#!/bin/bash
set -e

MAX_NUM_PARALLEL_OSD=9

PARTIAL_PIPELINE=" videotestsrc ! hailoupload pool-size=1 ! \
    imagefreeze ! video/x-raw,format=NV12,width=3840,height=2160 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailoosd config-file-path=/home/root/apps/basic_security_camera_streaming/resources/configs/osd_4k.json ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false sync=false "

PIPELINE="gst-launch-1.0 "

start_index=0
for ((n = $start_index; n < $MAX_NUM_PARALLEL_OSD; n++)); do
    PIPELINE+=$PARTIAL_PIPELINE
done

PIPELINE+=" -v"

echo ${PIPELINE}
eval ${PIPELINE}

