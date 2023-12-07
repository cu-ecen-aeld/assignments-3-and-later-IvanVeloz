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
TOOLCHAIN_LIBC_ROOT=$( dirname $( which ${CROSS_COMPILE}gcc))/../${CROSS_COMPILE%?}/libc

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
cp "${OUTDIR}/linux-stable/arch/arm64/boot/Image" "${OUTDIR}/Image"
echo "Creating the staging directory for the root filesystem"
cd "${OUTDIR}"
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




cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # DONE:  Configure busybox
    echo "Configuring busybox"
    make distclean
    make defconfig

else
    cd busybox
fi

# DONE: Make and install busybox
echo "Making busybox"
make \
    ARCH=${ARCH}\
    CROSS_COMPILE=${CROSS_COMPILE}
make \
    CONFIG_PREFIX="${OUTDIR}/rootfs"\
    ARCH=${ARCH}\
    CROSS_COMPILE=${CROSS_COMPILE}\
    install

# Next section seems to be working from rootfs but doesn't cd; so doing it now.
cd "$OUTDIR/rootfs"

echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# DONE: Add library dependencies to rootfs
#Library dependencies
#      [Requesting program interpreter: /lib/ld-linux-aarch64.so.1]
# 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
# 0x0000000000000001 (NEEDED)             Shared library: [libresolv.so.2]
# 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
# Program interpreter is placed in /lib directory
# Libraries are placed in /lib64 directory (since arch is 64 bit)
echo "Copying libraries to rootfs"
cd "${TOOLCHAIN_LIBC_ROOT}/lib"
cp ld-linux-aarch64.so.1\
    "${OUTDIR}/rootfs/lib/"

cd "${TOOLCHAIN_LIBC_ROOT}/lib64"
cp libm.so.6 libresolv.so.2 libc.so.6\
    "${OUTDIR}/rootfs/lib64/"


# DONE: Make device nodes
echo "Creating device node files in rootfs"
cd "${OUTDIR}/rootfs"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/console c 1 5
# NOTE: mknot did NOT work on a LXC container (unprivileged). If you are 
# running this script on a Docker or LXC container and get the error below:
#
# mknod: dev/null: Operation not permitted
#
# it's because unprivileges containers are not allowed to do this. I switched to
# a virtual machine at this point, on a faster computer.

# DONE: Clean and build the writer utility
echo "Building writer utility"
cd "${FINDER_APP_DIR}"
make clean
make

# DONE: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
echo "Copying finder related scripts and executables to /home"
cd "${FINDER_APP_DIR}"
cp writer finder.sh finder-test.sh autorun-qemu.sh\
    "${OUTDIR}/rootfs/home/"
mkdir -p "${OUTDIR}/rootfs/home/conf"
cp conf/assignment.txt conf/username.txt\
    "${OUTDIR}/rootfs/home/conf"


# DONE: Chown the root directory
# I referenced my Ubuntu installation for root filesystem owner and permissions.
echo "Changing rootfs ownership and permissions"
cd "${OUTDIR}/rootfs/"
chmod 755  bin etc home lib lib64 proc sbin sys tmp usr var\
           usr/bin usr/lib usr/sbin\
           var/log
           # don't touch the files
chmod 755 dev # don't touch the device nodes
sudo chown -R root:root "${OUTDIR}/rootfs/"

# TODO: Create initramfs.cpio.gz
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio