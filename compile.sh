#!/bin/bash

SSHFS="sudo sshfs"
BOARD_ROOT="/mnt/board"

GCC="arm-linux-gnueabi-"
BOARD_IP="beagle"
MAKE_ARGS=""
if [ x"$2" != x"" ]; then
    LINUX="-$2"
else
    LINUX=""
fi
KERNELDIR="/usr/src/beagle/linux$LINUX"

compile() {
    if [ -z "$MAKE_ARGS" ]; then
        make -C $KERNELDIR ARCH=arm M=$(pwd) CROSS_COMPILE=$GCC
    else
        make "$MAKE_ARGS" -C $KERNELDIR ARCH=arm M=$(pwd) CROSS_COMPILE=$GCC
    fi
}

install() {
    # test if sshf mounted
    # res=$(mount | grep $DEST)
    # if [ $? -eq 1 ]; then
    #     $SSHFS -o IdentityFile=/home/ben64/.ssh/id_rsa root@igep:/ $DEST
    #     [ $? -eq 1 ] && echo "Unable to mount sshfs" && exit 1
    # fi
    sudo umount $BOARD_ROOT
    $SSHFS -o IdentityFile=$HOME/.ssh/id_rsa root@$BOARD_IP:/ $BOARD_ROOT
    [ $? -eq 1 ] && echo "Unable to mount sshfs" && exit 1
    sudo make -C $KERNELDIR ARCH=arm M=$(pwd) ARCH=arm CROSS_COMPILE=$GCC modules_install INSTALL_MOD_PATH=$BOARD_ROOT
}

if [ $# -eq 1 -a x"$2" = x"-c" ]; then
    compile
else
    compile
    [ $? -ne 0 ] && exit 1
    install
fi
