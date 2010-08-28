#!/bin/sh
TESTS="gamma_set_5 gamma_set_6"

make -k -C $1 M=$PWD/kapitest clean >&2
make -k -C $1 M=$PWD/kapitest modules >&2

for i in $TESTS
do
	if [ -f kapitest/$i.o ]
	then
		echo \#define PSCNV_KAPI_`echo $i | tr a-z A-Z`
	fi
done
