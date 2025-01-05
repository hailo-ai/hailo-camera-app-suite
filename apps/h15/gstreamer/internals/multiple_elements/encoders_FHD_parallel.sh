#!/bin/bash
set -e

MAX_NUM_PARALLEL_ENCODERS=5

PARTIAL_PIPELINE=" videotestsrc ! hailoupload pool-size=2 ! \
    imagefreeze ! video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! \
    queue leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! \
    hailoh265enc ! h265parse config-interval=-1 ! video/x-h265,framerate=30/1 ! \
    queue leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false "

PIPELINE="gst-launch-1.0 "

start_index=0
for ((n = $start_index; n < $MAX_NUM_PARALLEL_ENCODERS; n++)); do
    PIPELINE+=$PARTIAL_PIPELINE
done

PIPELINE+=" -v"

echo ${PIPELINE}
eval ${PIPELINE}
