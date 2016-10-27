#!/bin/sh

if [ $# -eq 1 ]
then
  TYPE=$1
  case $TYPE in
    all )
      git pull lloth gingerbread
      cd ../dalvik
      git pull lloth gingerbread
      cd ../build
      git pull lloth gingerbread
      cd ../vm_test
      git pull lloth gingerbread
      cd ../libcore
      git pull lloth gingerbread
      cd ../marketsnatch
      git pull lloth gingerbread
      cd ../packages/apps/ToeManager
      git pull lloth gingerbread
      echo "Done pulling all from lloth"
      ;;
    dalvik )
      cd ../dalvik
      git pull lloth gingerbread
      echo "Done pulling dalvik from lloth"
      ;;
    build )
      cd ../build
      git pull lloth gingerbread
      echo "Done pulling build from lloth"
      ;;
    vm_test )
      cd ../vm_test
      git pull lloth gingerbread
      echo "Done pulling vm_test from lloth"
      ;;
    libcore )
      cd ../libcore
      git pull lloth gingerbread
      echo "Done pulling libcore from lloth"
      ;;
    scripts )
      cd ../scripts
      git pull lloth gingerbread
      echo "Done pulling scripts from lloth"
      ;;
    marketsnatch )
      cd ../marketsnatch
      git pull lloth gingerbread
      echo "Done pulling marketsnatch from lloth"
      ;;
    repo )
      cd ../.repo/manifests
      git pull origin master
      echo "Done pulling repo from origin"
      ;;
    * )
      echo "Invalid input."
      echo "Usage: $0 [all | dalvik | build | vm_test | libcore | scripts | marketsnatch | repo]"
      exit 2
      ;;
  esac
else
  echo "Pull on: [all | dalvik | build | vm_test | libcore | scripts | marketsnatch | repo]"
  read SELECT
  case $SELECT in
    all )
      git pull lloth gingerbread
      cd ../dalvik
      git pull lloth gingerbread
      cd ../build
      git pull lloth gingerbread
      cd ../vm_test
      git pull lloth gingerbread
      cd ../libcore
      git pull lloth gingerbread
      cd ../marketsnatch
      git pull lloth gingerbread
      echo "Done pulling all from lloth"
      ;;
    dalvik )
      cd ../dalvik
      git pull lloth gingerbread
      echo "Done pulling dalvik from lloth"
      ;;
    build )
      cd ../build
      git pull lloth gingerbread
      echo "Done pulling build from lloth"
      ;;
    vm_test )
      cd ../vm_test
      git pull lloth gingerbread
      echo "Done pulling vm_test from lloth"
      ;;
    libcore )
      cd ../libcore
      git pull lloth gingerbread
      echo "Done pulling libcore from lloth"
      ;;
    scripts )
      cd ../scripts
      git pull lloth gingerbread
      echo "Done pulling scripts from lloth"
      ;;
    marketsnatch )
      cd ../marketsnatch
      git pull lloth gingerbread
      echo "Done pulling marketsnatch from lloth"
      ;;
    repo )
      cd ../.repo/manifests
      git pull origin master
      echo "Done pulling repo from origin"
      ;;
    * )
      echo "Invalid input."
      echo "Usage: $0 [all | dalvik | build | vm_test | libcore | scripts | repo]"
      exit 2
      ;;
  esac
fi
