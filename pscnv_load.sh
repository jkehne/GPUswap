#!/bin/bash -eu
# -e: Exit immediately if a command exits with a non-zero status.
# -u: Treat unset variables as an error when substituting.

: ${PSCNV_NEEDS_KERNEL:="3.5.0-54-generic"}
# Parse options ( http://stackoverflow.com/questions/192249/how-do-i-parse-command-line-arguments-in-bash )
# -f Try inserting pscnv even if a possibly incompatible kernel is detected
# -x If pscnv happens to be already loaded, do nothing.
# -r Reclock
FORCE=0
DONOTRELOAD=0
RECLOCK=0
while getopts "fxr" opt; do
    case "$opt" in
    f)   FORCE=1
         ;;
    x)   DONOTRELOAD=1
         ;;
    r)   RECLOCK=1
    esac
done

shift $((OPTIND-1))

# If pscnv is already loaded and we are not supposed to reload it,
# exit silently
if [[ $DONOTRELOAD == 1 ]] && (lsmod | grep -q pscnv); then
  exit 0
fi

echo
echo -n "$(tput bold)"
echo    "This is: $0"
echo    "~~~ Checking kernel compatibility with pscnv..."
echo -n "$(tput sgr0)"

# Check if running an old enough kernel
if [[ ($FORCE == 0) && ("$(uname -r)" != "$PSCNV_NEEDS_KERNEL") ]]; then
 echo "Your kernel might be incompatible with pscnv!"
 echo
 echo "According to \"uname -r\" you are running:"
 echo -n "  "; uname -r
 echo "which doesn't match the expected value by this script:"
 echo "  $PSCNV_NEEDS_KERNEL"
 echo
 echo -n "$(tput bold)"
 echo "To skip this check, use -f on the command line."
 echo "Once you are sure that pscnv works with your kernel, "
 echo "you can set PSCNV_NEEDS_KERNEL=\"$(uname -r)\""
 echo "e.g. in your .bashrc, or just modify the default value"
 echo "of that variable at the beginning of this script."
 echo -n "$(tput sgr0)"
 echo
 echo -n "$(tput bold)$(tput setaf 1)"
 echo "Aborting $0... This script did nothing!"
 echo -n "$(tput sgr0)"
 exit 1
fi

echo
echo -n "$(tput bold)"
echo    "~~~ Checking for currently loaded GPU drivers..."
echo -n "$(tput sgr0)"
# Remove pscnv if it's currently loaded
if lsmod | grep -q pscnv; then
    echo "Pscnv is already loaded! Will attempt to remove now."
    if test "$(cat /sys/class/vtconsole/vtcon1/bind)" = "1"; then
      echo "Unbinding driver from console..."
      sudo bash -c 'echo 0 > /sys/class/vtconsole/vtcon1/bind'
    fi
    echo "Removing module (rmmod pscnv)..."
    sudo rmmod pscnv && echo "Pscnv successfully unloaded!"
fi

# Remove Nouveau if it's loaded
if lsmod | grep -q nouveau; then
    echo "Nouveau is loaded! Will attempt to remove now."
    if test "$(cat /sys/class/vtconsole/vtcon1/bind)" = "1"; then
      echo "Unbinding driver from console..."
      sudo bash -c 'echo 0 > /sys/class/vtconsole/vtcon1/bind'
    fi
    echo "Removing module (modprobe -r nouveau)..."
    sudo modprobe -rv nouveau && echo "Nouveau successfully unloaded!"
    #/etc/init.d/consolefont restart
fi

# Remove blob if it's loaded
if lsmod | grep -q nvidia; then
    echo "Nvidia blob is loaded! Will attempt to remove now."
    if lsmod | grep -q nvidia_uvm; then
        echo "Removing UVM module (rmmod nvidia_uvm)..."
        sudo rmmod nvidia_uvm
    fi
    echo "Removing main driver module (rmmod nvidia)..."
    sudo rmmod nvidia && echo "Nvidia blob successfully unloaded!"
    #/etc/init.d/consolefont restart
fi

# Reclock GPU to highest frequency
# See reclocking.txt
if [[ $RECLOCK == 1 ]]; then
    echo -n "$(tput bold)"
    echo "~~~ Patching GPU bios to mark all but the highest power state as invalid..."
    echo -n "$(tput sgr0)"
    sudo nvagetbios > vbios.rom
    nvbios vbios.rom > vbios.rom.dump
    # TODO: Automatically find out the parameters for nvafakebios from vbios.rom.dump
    NVAFAKEBIOS_PARAMS="-e 613c:ff -e 6180:ff -e 61c4:ff"
    rm vbios.rom.dump
    sudo nvafakebios $NVAFAKEBIOS_PARAMS vbios.rom
    rm vbios.rom

    echo -n "$(tput bold)"
    echo "~~~ Loading blob driver..."
    echo -n "$(tput sgr0)"
    #sudo modprobe -vf nvidia
    sudo insmod /lib/modules/$(uname -r)/updates/dkms/nvidia_340.ko
    sudo insmod /lib/modules/$(uname -r)/updates/dkms/nvidia-340-uvm.ko

    echo -n "$(tput bold)"
    echo "~~~ Starting a GPU application to force reclocking..."
    echo -n "$(tput sgr0)"
    PREVDIR=`pwd`
    cd ~jens/gdev/test/cuda/bfs
    ./bfs_nvidia
    cd $PREVDIR

    echo -n "$(tput bold)"
    echo "~~~ Unloading blob after reclocking..."
    echo -n "$(tput sgr0)"
    sudo rmmod nvidia-uvm
    sudo rmmod nvidia
fi

# Load dependencies
echo
echo -n "$(tput bold)"
echo    "~~~ Modprobing pscnv dependecies to ensure they are loaded..."
echo -n "$(tput sgr0)"
echo "Dependencies are: drm.ko drm_kms_helper.ko video.ko i2c-algo-bit.ko"
sudo modprobe -v drm
sudo modprobe -v drm_kms_helper
sudo modprobe -v video
sudo modprobe -v i2c-algo-bit

: ${PSCNV_BUILD_DIR:="$HOME/pscnv/build"}
PSCNV_KO_PATH="$PSCNV_BUILD_DIR/pscnv/pscnv.ko"
echo
echo -n "$(tput bold)"
echo    "~~~ Loading pscnv module (sudo insmod \"$PSCNV_KO_PATH\")..."
echo -n "$(tput sgr0)"
sudo insmod "$PSCNV_KO_PATH"

echo "Information from /sys/module/pscnv/sections:"
echo -n ".text: ";   sudo cat /sys/module/pscnv/sections/.text
echo -n ".data: ";   sudo cat /sys/module/pscnv/sections/.data
echo -n ".rodata: "; sudo cat /sys/module/pscnv/sections/.rodata
echo -n ".bss: ";    sudo cat /sys/module/pscnv/sections/.bss

echo
echo "$(tput bold)$(tput setaf 2)~~~ Success!$(tput sgr0)"
if [[ $RECLOCK == 0 ]]; then
echo "To reclock the GPU using the binary blob driver from nvidia and reload pscnv, run:"
echo "    \"$0\" -r"
fi
