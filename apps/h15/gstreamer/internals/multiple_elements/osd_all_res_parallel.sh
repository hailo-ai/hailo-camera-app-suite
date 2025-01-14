#!/bin/bash
set -e

FOUR_K_PIPELINE="\
    videotestsrc ! hailoupload pool-size=1 ! \
    imagefreeze ! video/x-raw,format=NV12,width=3840,height=2160 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailoosd config-file-path=/home/root/apps/basic_security_camera_streaming/resources/configs/osd_4k.json ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false sync=false "
FHD_PIPELINE="\
    videotestsrc ! hailoupload pool-size=1 ! \
    imagefreeze ! video/x-raw,format=NV12,width=1920,height=1080 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailoosd config-file-path=/home/root/apps/basic_security_camera_streaming/resources/configs/osd_fhd.json ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false sync=false "
HD_PIPELINE="\
    videotestsrc ! hailoupload pool-size=1 ! \
    imagefreeze ! video/x-raw,format=NV12,width=1280,height=720 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailoosd config-file-path=/home/root/apps/basic_security_camera_streaming/resources/configs/osd_hd.json ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false sync=false"
SD_PIPELINE="\
    videotestsrc ! hailoupload pool-size=1 ! \
    imagefreeze ! video/x-raw,format=NV12,width=640,height=480 ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    hailoosd config-file-path=/home/root/apps/basic_security_camera_streaming/resources/configs/osd_sd.json ! \
    queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! \
    fpsdisplaysink fps-update-interval=2000 video-sink=fakesink text-overlay=false sync=false "

PIPELINE="gst-launch-1.0 \
    ${FOUR_K_PIPELINE} ${FOUR_K_PIPELINE} \
    ${FHD_PIPELINE} ${FHD_PIPELINE} ${FHD_PIPELINE} \
    ${HD_PIPELINE} ${HD_PIPELINE} ${HD_PIPELINE} \
    ${SD_PIPELINE} ${SD_PIPELINE} ${SD_PIPELINE} -v"

echo ${PIPELINE}
eval ${PIPELINE}
