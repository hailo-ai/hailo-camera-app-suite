#!/bin/bash
set -e

FOUR_K_PIPELINE="\
    videotestsrc ! hailoupload pool-size=5 ! \
    imagefreeze ! video/x-raw,format=NV12,width=3840,height=2160,framerate=30/1 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailoh265enc ! h265parse config-interval=-1 ! video/x-h265,framerate=30/1 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false "
FHD_PIPELINE="\
    videotestsrc ! hailoupload pool-size=5 ! \
    imagefreeze ! video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailoh265enc ! h265parse config-interval=-1 ! video/x-h265,framerate=30/1 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false "
HD_PIPELINE="\
    videotestsrc ! hailoupload pool-size=5 ! \
    imagefreeze ! video/x-raw,format=NV12,width=1280,height=720,framerate=30/1 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailoh265enc ! h265parse config-interval=-1 ! video/x-h265,framerate=30/1 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false "
SD_PIPELINE="\
    videotestsrc ! hailoupload pool-size=5 ! \
    imagefreeze ! video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailoh265enc ! h265parse config-interval=-1 ! video/x-h265,framerate=30/1 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false "

PIPELINE="gst-launch-1.0 \
    ${FOUR_K_PIPELINE} \
    ${FHD_PIPELINE} \
    ${HD_PIPELINE} \
    ${SD_PIPELINE} -v"

echo ${PIPELINE}
eval ${PIPELINE}
