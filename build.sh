#! /bin/sh
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
mkdir -p /lib/modules/$(uname -r)/extra
cp pscnv/pscnv.ko /lib/modules/$(uname -r)/extra
depmod
cd ../test
make clean
make
cd ..
