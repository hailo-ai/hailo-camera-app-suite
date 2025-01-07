#!/bin/bash
set -e

PIPELINE="gst-launch-1.0 videotestsrc ! hailoupload pool-size=5 ! \
    imagefreeze ! video/x-raw,format=NV12,width=3840,height=2160,framerate=30/1 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailoh265enc ! h265parse config-interval=-1 ! video/x-h265,framerate=30/1 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false \
    videotestsrc ! hailoupload pool-size=5 ! \
    imagefreeze ! video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailoh265enc ! h265parse config-interval=-1 ! video/x-h265,framerate=30/1 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false -v"

echo ${PIPELINE}
eval ${PIPELINE}
