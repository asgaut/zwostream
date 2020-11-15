#!/usr/bin/env bash
set -e
NAME=ASI_linux_mac_SDK_V1.15.0915.tar.bz2
wget https://astronomy-imaging-camera.com/software/$NAME
mkdir sdk
tar xvf $NAME -C sdk --strip 1
rm $NAME
