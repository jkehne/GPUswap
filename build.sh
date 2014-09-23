#!/bin/bash -eu
# -e: Exit immediately if a command exits with a non-zero status.
# -u: Treat unset variables as an error when substituting.

# Builds pscnv and libpscnv. Currently doesn't install anything anywhere,
# but optionally (-s command line switch) calls pscnv_load.sh to load up
# the produced module pscnv.ko directly from the build folder

# INPUT VARIABLES
# ===============
# Compiles from
# $PSCNV_SOURCE_DIR  (default $HOME/pscnv )
# in build directory:
# $PSCNV_BUILD_DIR   (default $PSCNV_SOURCE_DIR/build )

# COMMAND LINE
# ============
# -c Clean thoroughly before building
# -l Call pscnv_load.sh after building

DOCLEAN=0
DOLOAD=0
while getopts "cl" opt; do
    case "$opt" in
    c)   DOCLEAN=1
         ;;
    l)   DOLOAD=1
         ;;
    esac
done

# SCRIPT BODY
# ==========

# Search "man bash" for "Assign Default Values" to understand these lines:
: ${PSCNV_SOURCE_DIR:="$HOME/pscnv"}
: ${PSCNV_BUILD_DIR:="$PSCNV_SOURCE_DIR/build"}

echo -n "$(tput bold)"
echo    "~~~ Dumping input variables and performing some checks..."
echo -n "$(tput sgr0)"
echo    "PSCNV_SOURCE_DIR=$PSCNV_SOURCE_DIR"
echo    "PSCNV_BUILD_DIR=$PSCNV_BUILD_DIR"
echo
echo    "Will compile against your current kernel version: $(uname -r)"

#Short aliases
SRC_DIR="$PSCNV_SOURCE_DIR"
BUILD_DIR="$PSCNV_BUILD_DIR"

# Clean old stuff up
if [[ $DOCLEAN == 1 ]]; then
    echo
    echo "$(tput bold)~~~ Cleaning $SRC_DIR/pscnv (make clean; make distclean)...$(tput sgr0)"
    cd "$SRC_DIR/pscnv"
    make clean
    make distclean

    echo
    echo "$(tput bold)~~~ Cleaning $SRC_DIR/libpscnv (make clean)...$(tput sgr0)"
    cd "$SRC_DIR/libpscnv"
    make clean

    echo
    echo "$(tput bold)~~~ Cleaning $SRC_DIR/test (make clean)...$(tput sgr0)"
    cd "$SRC_DIR/test"
    make clean

    echo
    echo "$(tput bold)~~~ Removing $BUILD_DIR...$(tput sgr0)"
    rm -rf "$BUILD_DIR"
fi

# Recreate build directory
echo
echo "$(tput bold)~~~ Entering $BUILD_DIR...$(tput sgr0)"
mkdir -pv "$BUILD_DIR"
cd "$BUILD_DIR"

# cmake
echo
echo "$(tput bold)~~~ Invoking cmake $SRC_DIR...$(tput sgr0)"
cmake "$SRC_DIR"

# make driver and library
echo
echo "$(tput bold)~~~ Invoking make...$(tput sgr0)"
make

# make test
#echo
#echo "$(tput bold)~~~ Building tests...$(tput sgr0)"
#cd "$SRC_DIR/test"
#make

# Success
PSCNV_KO_PATH="$BUILD_DIR/pscnv/pscnv.ko"
echo
echo -n "$(tput bold)$(tput setaf 2)"
echo "~~~ Success! Pscnv was built!"
echo -n "$(tput sgr0)"

if [[ $DOLOAD == 1 ]]; then
    $SRC_DIR/pscnv_load.sh
else
    echo "To install and load into kernel: "
    echo "    sudo mkdir -p \"/lib/modules/\$(uname -r)/extra\""
    echo "    sudo cp \"$PSCNV_KO_PATH\" \"/lib/modules/\$(uname -r)/extra\""
    echo "    sudo depmod"
    echo "    sudo modprobe pscnv"
    echo
    echo "To load into kernel directly without installing:"
    echo "    sudo modprobe -v drm"
    echo "    sudo modprobe -v drm_kms_helper"
    echo "    sudo modprobe -v video"
    echo "    sudo modprobe -v i2c-algo-bit"
    echo "    sudo insmod \"$PSCNV_KO_PATH\""
    echo
    echo "To (re)load using a smart script that can also do reclocking (-r):"
    echo "    \"$SRC_DIR/pscnv_load.sh\"" # -r
fi

