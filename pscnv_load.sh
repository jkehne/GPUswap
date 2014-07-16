#! /bin/bash

# see reclocking.txt 
#
# after reading it, you should have a command like the following:

/usr/local/bin/nvafakebios -e 613c:ff -e 6180:ff -e 61c4:ff vbios.rom

# this will load vbios.rom, overwrite the bytes as given by -e parameters
# and upload the modified bios image to the card

insmod nvidia.ko

# some calculation to force reclocking (almost anything should work)
cd ~/gdev/test/cuda/bfs
./bfs_nvidia

# nvidia-uvm gets automatically loaded
rmmod nvidia-uvm
rmmod nvidia

# the debug *_debug=? driver options just control pscnv's verbosity
#
# theoretically it shuld be also possible to load nouveau at this point
#
# Both stock pscnv and nouveau will fail to initialize, as the blob leaves
# the card (video output code?) in some strange state that the open- source
# drivers can't handle.
# Simple solution: never mind! the pscnv in this branch is modified so that it
# never even tries to initialize the screen, which triggers the fault.
# All the GPGPU stuff is unaffected by this
insmod ~/pscnv/pscnv/pscnv.ko vm_debug=0 pause_debug=2

# see dmesg output: pscnv print's available performance levels on init.
# the line prefixed with 'c:' is the current level