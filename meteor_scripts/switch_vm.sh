#!/bin/sh

if [ $# -eq 1 ]
then
  MODE=$1
  VMDIR=$ANDROID_BUILD_TOP/dalvik/vm
  
  cd $VMDIR
  rm -f Custom.mk
  case $MODE in
    none )
      ln -s /dev/null Custom.mk
      ;;
    offload )
      ln -s Offload.mk Custom.mk
      ;;
    offload-debug )
      ln -s OffloadDebug.mk Custom.mk
      ;;
    replay )
      ln -s Replay.mk Custom.mk
      ;;
    replay-profile)
      ln -s ReplayProfile.mk Custom.mk
      ;;

    * )
      echo "Target not found"
      exit 1
      ;;
  esac
else
  echo "Usage: $0 MODE"
  echo "Example: $0 offload"
  echo "Supported modes are: none, offload, replay, replay-profile"
fi
