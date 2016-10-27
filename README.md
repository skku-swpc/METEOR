# METEOR
A Multi-Node Multithreading Computation Offloading Framework. A Multitheading and Multi-node extenstion for [COMET framework] (http://www.cs.columbia.edu/~lierranli/coms6998-10Spring2013/papers/comet_osdi2012.pdf).

# How to install
## Donwload/Build Dalvik JVM

1. Make your source code directory

  $ mkdir meteor_source
  
  $ cd meteor_source
  
2. Download CyanogenMod for Galaxy Nexus (Maguro) and setup build environment(refer [this] (https://wiki.cyanogenmod.org/w/Build_for_maguro)).

3. Clone this repository and copy METEOR into your android source tree

  $ git clone https://github.com/skku-swpc/METEOR/ .
  
  $ cp -r METEOR/meteor_dalvik your_android_source_tree/
  
  $ cp -r METEOR/meteor_scripts your_android_source_tree/
  
4. Replace original Dalvik with one that copied from 3

  $ cd your_android_source_tree/
  
  $ mv dalvik original_dalvik
  
  $ mv meteor_dalvik dalvik

5. build Dalvik with following command

  $ make -j$(nproc) libdvm

## Install Dalvik JVM to the device and servers

### Device

1. Open 30tcpmux file in the script folder

  $ vi meteor_scripts/30tcpmux
2. Modify IP address in the file. The server that has the IP address will be the first repartitioner node.

  if [ -e /system/bin/tcpmux ];
  
  then
  
   tcpmux --daemon --control 5554 --retry 5555 12.34.56.78:5556
   
  fi;

 In this example, The node 12.34.56.78 will be the first repartitioner node.

3. Execute the following command. make sure that 1. your device and computer are connected 2. adb is installed in your computer.

   $ meteor_scripts/push_vm.sh
   
### Server

Type following scripts in the script folder (for each server).

  $ ./setup_links.sh
  
  $./setup_server.sh myconfig prepare
  
# How to run

Make sure that your device is coneected to Internet. And run following scripts in the script folder (for each server).

  $ system/bin/tcpmux --daemon --demux 5556 localhost:12345
  
  $ ./stdrunserv.sh 5556 12345

# Contact

Jaemin Lee, Ph.D student at Compute System Lab. Sungkyunkwan Univ. (jaeminyx@csl.skku.edu)
