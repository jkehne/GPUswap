#! /bin/bash
#KERNELVER=$(uname -r)
KERNELVER="3.5.7.31"

. /etc/profile
. ~/.bashrc

cd pscnv
make clean
cd ../libpscnv
make clean
cd ..
rm -rf build
mkdir build
cd build
cmake ..
make clean
make
mkdir -p /lib/modules/$KERNELVER/extra
cp pscnv/pscnv.ko /lib/modules/$KERNELVER/extra
depmod
#cd ../test
#make clean
#make
#cd ..
