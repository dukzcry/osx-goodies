#!/bin/sh

#brew install bsdmake

#TMP=`mktemp -d /tmp/XXXXXX`
TMP=`mkdir -p /tmp/mfi; echo /tmp/mfi`
INC="/usr/include"
LU="libutil-30"
XNU="xnu-2050.18.24"
CFLAGS=${CFLAGS}" -D_SYS_CDEFS_H_ -DKERNEL -I. \
	-include ${INC}/stdint.h -include ${INC}/sys/types.h \
	\"-D__packed=__attribute__((packed))\" -v"
#-Dlint
export CFLAGS

svn co svn://svn.freebsd.org/base/stable/9/usr.sbin/mfiutil mfiutil

svn co svn://svn.freebsd.org/base/stable/9/sys/sys ${TMP}/sys
svn co svn://svn.freebsd.org/base/stable/9/sys/dev/mfi ${TMP}/mfi
svn co svn://svn.freebsd.org/base/stable/9/sys/cam/scsi ${TMP}/scsi
[ ! -e ${TMP}/${LU}.tgz ] && \
	curl http://www.opensource.apple.com/tarballs/libutil/${LU}.tar.gz -o ${TMP}/${LU}.tgz
[ ! -e ${TMP}/${XNU}.tgz ] && \
	curl http://www.opensource.apple.com/tarballs/xnu/${XNU}.tar.gz -o ${TMP}/${XNU}.tgz

cd mfiutil
tar xzf ${TMP}/${LU}.tgz -C .
tar xzf ${TMP}/${XNU}.tgz -C .
mkdir ./sys
mkdir machine
mkdir -p ./dev/mfi
mkdir -p ./cam/scsi

mkdir ./libkern
ln -s ${TMP}/sys/param.h ${TMP}/sys/_null.h \
	./sys/
#${TMP}/sys/_stdint.h
ln -s ${TMP}/scsi/scsi_all.h ./cam/scsi/
ln -s ${TMP}/mfi/mfireg.h ${TMP}/mfi/mfi_ioctl.h ./dev/mfi/

#ln -s ${INC}/i386/param.h ./sys/
ln -s ${INC}/stdint.h ./sys/_stdint.h
ln -s ${INC}/sys/syslimits.h ./sys/limits.h
# Apple does not appear to provide llvm-config
ln -s /System/Library/Frameworks/Kernel.framework/Versions/A/Headers/stdarg.h ./machine/
ln -s ./${LU}/libutil.h .
ln -s ${PWD}/${XNU}/bsd/sys/linker_set.h ./sys/
ln -s ${PWD}/${XNU}/libkern/libkern/kernel_mach_header.h ./libkern/

#CC=gcc 
bsdmake
