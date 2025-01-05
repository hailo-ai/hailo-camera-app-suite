#!/bin/bash
set -e

MAX_NUM_PARALLEL_OSD=16

PARTIAL_PIPELINE=" videotestsrc ! hailoupload pool-size=1 ! \
    imagefreeze ! video/x-raw,format=NV12,width=1920,height=1080 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailoosd config-file-path=/home/root/apps/basic_security_camera_streaming/resources/configs/osd_fhd.json ! \
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


