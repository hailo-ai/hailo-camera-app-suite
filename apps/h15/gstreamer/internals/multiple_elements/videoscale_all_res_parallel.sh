#!/bin/bash
set -e

FOURK_2_FHD_PIPELINE="\
    videotestsrc ! hailoupload pool-size=2 ! \
    imagefreeze ! video/x-raw,format=NV12,width=3840,height=2160 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailovideoscale pool-size=5 ! video/x-raw,format=NV12,width=1920,height=1080 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false sync=false "
FHD_2_HD_PIPELINE="\
    videotestsrc ! hailoupload pool-size=2 ! \
    imagefreeze ! video/x-raw,format=NV12,width=1920,height=1080 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailovideoscale pool-size=5 ! video/x-raw,format=NV12,width=1280,height=720 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false sync=false "
HD_2_SD_PIPELINE="\
    videotestsrc ! hailoupload pool-size=2 ! \
    imagefreeze ! video/x-raw,format=NV12,width=1280,height=720 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailovideoscale pool-size=5 ! video/x-raw,format=NV12,width=640,height=480 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false sync=false "

PIPELINE="gst-launch-1.0 \
    ${FOURK_2_FHD_PIPELINE} \
    ${FHD_2_HD_PIPELINE} ${FHD_2_HD_PIPELINE} \
    ${HD_2_SD_PIPELINE} ${HD_2_SD_PIPELINE} -v"

echo ${PIPELINE}
eval ${PIPELINE}
