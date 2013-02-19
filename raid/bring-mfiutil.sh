#!/bin/sh

#brew install bsdmake

#TMP=`mktemp -d /tmp/XXXXXX`
TMP=`mkdir -p /tmp/mfi; echo /tmp/mfi`
INC="/usr/include"
export CFLAGS=${CFLAGS}" -D_SYS_CDEFS_H_ -Dlint -I. \
	-include ${INC}/stdint.h -include ${INC}/sys/types.h \
	\"-D__packed=__attribute__((packed))\""

svn co svn://svn.freebsd.org/base/stable/9/usr.sbin/mfiutil mfiutil

svn co svn://svn.freebsd.org/base/stable/9/sys/sys ${TMP}/sys
svn co svn://svn.freebsd.org/base/stable/9/sys/dev/mfi ${TMP}/mfi
svn co svn://svn.freebsd.org/base/stable/9/sys/cam/scsi ${TMP}/scsi
#svn co svn://svn.freebsd.org/base/stable/9/lib/libutil ${TMP}/libutil

cd mfiutil
mkdir ./sys
mkdir machine
mkdir -p ./dev/mfi
mkdir -p ./cam/scsi
ln -s ${TMP}/sys/linker_set.h ${TMP}/sys/param.h ${TMP}/sys/_null.h \
	./sys/
#${TMP}/sys/_stdint.h
ln -s ${TMP}/scsi/scsi_all.h ./cam/scsi/
ln -s ${TMP}/mfi/mfireg.h ${TMP}/mfi/mfi_ioctl.h ./dev/mfi/
#ln -s ${TMP}/libutil/libutil.h .
touch libutil.h

ln -s ${INC}/i386/param.h ./sys/
ln -s ${INC}/stdint.h ./sys/_stdint.h
ln -s ${INC}/sys/syslimits.h ./sys/limits.h
# Apple does not appear to provide llvm-config
ln -s /System/Library/Frameworks/Kernel.framework/Versions/A/Headers/stdarg.h ./machine/
bsdmake
