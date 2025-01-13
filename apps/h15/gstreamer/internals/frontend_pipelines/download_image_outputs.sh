#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    readonly DEFAULT_ADDRESS="10.0.0.1"
    readonly DEFAULT_REMOTE_DIR="/tmp/images_output"

    address=$DEFAULT_ADDRESS
    local_dir=$CURRENT_DIR
    remote_dir=$DEFAULT_REMOTE_DIR

    print_gst_launch_only=false
    additional_parameters=""
}

function print_usage() {
    echo "Download remote :"
    echo ""
    echo "Options:"
    echo "  --help                        Show this help"
    echo "  -a ADDRESS --address ADDRESS  Set the address source (default $address)"
    echo "  -l PATH --local-dir PATH      Set the local path (default $local_dir)"
    echo "  -r PATH --remote-dir PATH     Set the remote path (default $remote_dir)"
    exit 0
}

function parse_args() {
    while test $# -gt 0; do
        if [ "$1" = "--help" ] || [ "$1" == "-h" ]; then
            print_usage
            exit 0
        elif [ "$1" = "--address" ] || [ "$1" = "-a" ]; then
            address="$2"
            shift
        elif [ "$1" = "--local-dir" ] || [ "$1" = "-l" ]; then
            local_dir="$2"
            shift
        elif [ "$1" = "--remote-dir" ] || [ "$1" = "-r" ]; then
            remote_dir="$2"
            shift
        else
            echo "Received invalid argument: $1. See expected arguments below:"
            print_usage
            exit 1
        fi

        shift
    done
}

init_variables $@
parse_args $@

trailing_dir=$(basename "$remote_dir")
echo "Cleaning $local_dir"/"$trailing_dir"
rm -rf ${local_dir}/${trailing_dir}

echo "Downloading from root@$address:$remote_dir to $local_dir"
scp -r root@$address:$remote_dir $local_dir
