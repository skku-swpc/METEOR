#!/bin/sh

OUTDIR=$OUT
if [ $# -eq 1 ]
then
  OUTDIR=../out/target/product/$DEVICE
fi

rm -f $OUTDIR/system/lib/libdvm.so
rm -rf $OUTDIR/obj/SHARED_LIBRARIES/libdvm_intermediates
rm -f $ANDROID_HOST_OUT/system/lib/libdvm.so
rm -rf $ANDROID_HOST_OUT/obj/SHARED_LIBRARIES/libdvm_intermediates
