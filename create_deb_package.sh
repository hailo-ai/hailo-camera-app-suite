#!/bin/bash
set -e
# Check if we are running on Raspberry Pi
if uname -a | grep -q 'raspberrypi'; then
    echo "Running on Raspberry Pi"
    target_platform='rpi5'
else
    target_platform=`uname -m`
fi
echo "Target platform: $target_platform"

if [[ -z "$TAPPAS_WORKSPACE" ]]; then
  export TAPPAS_WORKSPACE=$(dirname "$(realpath "$0")")
  echo "No TAPPAS_WORKSPACE in environment found, using the default one $TAPPAS_WORKSPACE"
fi

readonly VENV_NAME='hailo_tappas_venv'
readonly VENV_PATH="$(pwd)"
readonly TAPPAS_BASH_ENV=${HAILO_USER_DIR}/tappas_env
readonly TAPPAS_VERSION=$(grep -a1 project core/hailo/meson.build | grep version | cut -d':' -f2 | tr -d "', ")

function python_venv_create_and_install() {
  # Check if a virtual environment is active
  if [ ! -z "$VIRTUAL_ENV" ]; then
      echo "Packing with active virtualenv: $VIRTUAL_ENV"
  elif [ -d "${VENV_PATH}/${VENV_NAME}" ]; then
      echo "Using existing venv: ${VENV_PATH}/${VENV_NAME}"
      # Activate the virtual environment
      source "${VENV_PATH}/${VENV_NAME}/bin/activate"
  else
      echo "No active virtualenv found and virtual environment not created in the previous script. Exiting."
      exit 1
  fi


  # Your additional script code here, using the activated virtual environment as needed
  # For example:
  # pip install some_package
  # python script.py
}

function create_deb_package() {
    python3 $TAPPAS_WORKSPACE/packaging/deb/create_deb_package.py --tappas-version ${TAPPAS_VERSION} --arch ${target_platform} $@
}


function setup_pkg_config(){
  ./scripts/misc/pkg_config_setup.sh \
    --tappas-workspace "null" \
    --tappas-version ${TAPPAS_VERSION} --core-only true # This is hard-coded as we don't expect to build a deb with full tappas :)
}

function print_header()
{
    echo "========================================"
    echo "Starting TAPPAS DEB Package Creation"
    echo "========================================"

}

function print_footer()
{
    echo "========================================"
    echo "TAPPAS DEB Package Creation Completed"
    echo "========================================"
}

function main() {
    print_header
    python_venv_create_and_install
    setup_pkg_config
    create_deb_package $@
    print_footer
}

main $@