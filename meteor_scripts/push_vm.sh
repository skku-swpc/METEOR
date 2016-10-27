#!/bin/sh

OUTDIR=$OUT
if [ $# -eq 1 ]
then
  OUTDIR=../out/target/product/$DEVICE
fi


LIBDIR=$OUTDIR/system/lib
BINDIR=$OUTDIR/system/bin
APPDIR=$OUTDIR/system/app
TMPDIR=/tmp

echo "Pushing dvm image from $OUTDIR"

adb root && sleep 1 && adb wait-for-device remount                    && \
adb push $LIBDIR/libdvm.so /system/lib/libdvm.so                      && \
adb push $LIBDIR/libnativehelper.so /system/lib/libnativehelper.so    && \
adb push $BINDIR/dalvikvm /system/bin/dalvikvm                        && \
adb push $BINDIR/dexopt /system/bin/dexopt                            && \
adb push $BINDIR/app_process /system/bin/app_process                  && \
adb push $BINDIR/tcpmux /system/bin/tcpmux                            && \
adb push $BINDIR/cometmanager /system/bin/cometmanager                && \
adb push $APPDIR/CometSettings.apk /system/app/CometSettings.apk      && \
adb push $APPDIR/Settings.apk /system/app/Settings.apk                && \
adb push $OUTDIR/system/framework/core.jar /system/framework/core.jar && \
adb shell chmod 4555 /system/bin/cometmanager                         && \
adb push $ANDROID_BUILD_TOP/scripts/30tcpmux /system/etc/init.d/30tcpmux && \
adb shell chmod 750 /system/etc/init.d/30tcpmux                       && \
adb reboot                                                            && \
echo OK                                                               && \
exit

echo "Push failed"
exit 1
