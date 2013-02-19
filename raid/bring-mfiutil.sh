#!/bin/sh

#brew install bsdmake

#TMP=`mktemp -d /tmp/XXXXXX`
TMP=`mkdir -p /tmp/mfi; echo /tmp/mfi`
INC="/usr/include"
LU="libutil-30"
CFLAGS=${CFLAGS}" -D_SYS_CDEFS_H_ -Dlint -I. \
	-include ${INC}/stdint.h -include ${INC}/sys/types.h \
	\"-D__packed=__attribute__((packed))\""
LDFLAGS=${LDFLAGS}" -L./${LU}/dst/usr/lib"
export CFLAGS LDFLAGS

svn co svn://svn.freebsd.org/base/stable/9/usr.sbin/mfiutil mfiutil

svn co svn://svn.freebsd.org/base/stable/9/sys/sys ${TMP}/sys
svn co svn://svn.freebsd.org/base/stable/9/sys/dev/mfi ${TMP}/mfi
svn co svn://svn.freebsd.org/base/stable/9/sys/cam/scsi ${TMP}/scsi
curl http://www.opensource.apple.com/tarballs/libutil/${LU}.tar.gz -o ${TMP}/${LU}.tgz

cd mfiutil
tar xzf ${TMP}/${LU}.tgz -C .
mkdir ./sys
mkdir machine
mkdir -p ./dev/mfi
mkdir -p ./cam/scsi
ln -s ${TMP}/sys/linker_set.h ${TMP}/sys/param.h ${TMP}/sys/_null.h \
	./sys/
#${TMP}/sys/_stdint.h
ln -s ${TMP}/scsi/scsi_all.h ./cam/scsi/
ln -s ${TMP}/mfi/mfireg.h ${TMP}/mfi/mfi_ioctl.h ./dev/mfi/

ln -s ${INC}/i386/param.h ./sys/
ln -s ${INC}/stdint.h ./sys/_stdint.h
ln -s ${INC}/sys/syslimits.h ./sys/limits.h
# Apple does not appear to provide llvm-config
ln -s /System/Library/Frameworks/Kernel.framework/Versions/A/Headers/stdarg.h ./machine/
ln -s ./${LU}/libutil.h .

#cd ./${LU}
#mkdir -p obj sym dat
#xcodebuild install -target util ARCHS="i386 x86_64" \
#	SRCROOT=$PWD OBJROOT=$PWD/obj SYMROOT=$PWD/sym DSTROOT=$PWD/dst
#cd ..

bsdmake
