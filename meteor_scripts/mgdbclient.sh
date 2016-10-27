#!/bin/sh

# Make sure you have your build environment setup with lunch.  Additionally make
# sure you have build app_process (i.e. 'make app_process') for the device and
# that your local build reflects what is on the device.
#
# Usage:
# ./mgdbclient.sh PID
#
# Use logcat or ps on the device to determine the PID to debug.

PID=$1
PORT=10738 # You can set port to whatever
SYMBOLS_ROOT=$OUT/symbols
COMMAND_FILE="/tmp/gdbclient.cmds"

adb forward tcp:$PORT tcp:$PORT
#adb shell 'gdbserver --attach :$PORT $PID' &

echo >|$COMMAND_FILE "set solib-absolute-prefix $SYMBOLS_ROOT"
echo >>$COMMAND_FILE "set solib-search-path $SYMBOLS_ROOT/system/lib"
echo >>$COMMAND_FILE "target remote localhost:$PORT"
echo >>$COMMAND_FILE ""

arm-eabi-gdb -x $COMMAND_FILE "$SYMBOLS_ROOT/system/bin/app_process"
