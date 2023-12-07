#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # DONE: Add your kernel build steps here
    NPROC=$(nproc --all) # get the number of processors to speed up compilation
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper #cleanup
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig #assign defconfig
    make -j ${NPROC} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} Image #vmlinux
    # I really wish I had done this homework on a faster computer...
fi

echo "Adding the Image in outdir"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi



# DONE: Create necessary base directories
mkdir "${OUTDIR}/rootfs"

cd "${OUTDIR}/rootfs/"
mkdir bin dev etc home lib lib64 proc sbin sys tmp usr var

cd "${OUTDIR}/rootfs/usr"
mkdir bin lib sbin

cd "${OUTDIR}/rootfs/var"
mkdir log

echo "rootfs directory tree:"
tree "${OUTDIR}/rootfs"

echo "changing rootfs ownership and permissions"
# Not sure if this is necessary but it seemed wrong not to do it. After all, the
# rootfs is going into a compressed image and should preserve the ownership and
# permissions we set here.
#
# I referenced my Ubuntu installation for root filesystem owner and permissions.
# Personally, I prefer only running the single chown as root, instead of all 
# the mkdirs. Less lines running as root.
chmod -R 755 "${OUTDIR}/rootfs/"
sudo chown -R root:root "${OUTDIR}/rootfs/"


cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox

else
    cd busybox
fi

# TODO: Make and install busybox

echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs

# TODO: Make device nodes

# TODO: Clean and build the writer utility

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs

# TODO: Chown the root directory

# TODO: Create initramfs.cpio.gz
