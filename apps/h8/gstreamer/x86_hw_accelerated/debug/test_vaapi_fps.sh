#!/bin/bash
rgba_support=0
if [ $(lsb_release -r | awk '{print $2}' | awk -F'.' '{print $1}') -eq 22 ]; then
   rgba_support=1
else 
    if [ $(lsb_release -r | awk '{print $2}' | awk -F'.' '{print $1}') -ne 20 ]; then
        echo Unsupported ubuntu version
        exit 1
    fi
fi

./copy_videos.sh apps/h8/gstreamer/x86_hw_accelerated/debug/detection.mp4
./multistream_decode.sh --num-of-sources 10 --show-fps --format NV12 --resolution YOLO
./multistream_decode.sh --num-of-sources 20 --show-fps --format NV12 --resolution YOLO
./multistream_decode.sh --num-of-sources 10 --show-fps --format YUY2 --resolution YOLO
./multistream_decode.sh --num-of-sources 20 --show-fps --format YUY2 --resolution YOLO
if [ $rgba_support -eq 1 ]; then
    ./multistream_decode.sh --num-of-sources 10 --show-fps --format RGBA --resolution YOLO
    ./multistream_decode.sh --num-of-sources 20 --show-fps --format RGBA --resolution YOLO
fi

./copy_videos.sh apps/h8/gstreamer/x86_hw_accelerated/debug/face_detection.mp4
./multistream_decode.sh --num-of-sources 10 --show-fps --format NV12 --resolution HD
./multistream_decode.sh --num-of-sources 20 --show-fps --format NV12 --resolution HD
./multistream_decode.sh --num-of-sources 10 --show-fps --format YUY2 --resolution HD
./multistream_decode.sh --num-of-sources 20 --show-fps --format YUY2 --resolution HD
if [ $rgba_support -eq 1 ]; then
    ./multistream_decode.sh --num-of-sources 10 --show-fps --format RGBA --resolution HD
    ./multistream_decode.sh --num-of-sources 20 --show-fps --format RGBA --resolution HD
fi

./copy_videos.sh apps/h8/gstreamer/x86_hw_accelerated/debug/lpr_ayalon.mp4
./multistream_decode.sh --num-of-sources 10 --show-fps --format NV12
./multistream_decode.sh --num-of-sources 20 --show-fps --format NV12
./multistream_decode.sh --num-of-sources 10 --show-fps --format YUY2
./multistream_decode.sh --num-of-sources 20 --show-fps --format YUY2
if [ $rgba_support -eq 1 ]; then
    ./multistream_decode.sh --num-of-sources 10 --show-fps --format RGBA
    ./multistream_decode.sh --num-of-sources 20 --show-fps --format RGBA
fi

