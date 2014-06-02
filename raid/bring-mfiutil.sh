#!/bin/sh
# $dukzcry$

#brew install bsdmake

#TMP=`mktemp -d /tmp/XXXXXX`
TMP=`mkdir -p /tmp/mfi; echo /tmp/mfi`
INC="/usr/include"
LU="libutil-30"
XNU="xnu-2050.18.24"
CFLAGS=${CFLAGS}" -D_SYS_CDEFS_H_ -I. \
	-include ${INC}/stdint.h -include ${INC}/sys/types.h \
	\"-D__packed=__attribute__((packed))\" -v \
	-arch x86_64 -arch i386"
#-Dlint
export CFLAGS

svn co svn://svn.freebsd.org/base/release/9.1.0/usr.sbin/mfiutil mfiutil

svn co svn://svn.freebsd.org/base/release/9.1.0/sys/sys ${TMP}/sys
svn co svn://svn.freebsd.org/base/release/9.1.0/sys/dev/mfi ${TMP}/mfi
svn co svn://svn.freebsd.org/base/release/9.1.0/sys/cam/scsi ${TMP}/scsi
[ ! -e ${TMP}/${LU}.tgz ] && \
	curl http://www.opensource.apple.com/tarballs/libutil/${LU}.tar.gz -o ${TMP}/${LU}.tgz
[ ! -e ${TMP}/${XNU}.tgz ] && \
	curl http://www.opensource.apple.com/tarballs/xnu/${XNU}.tar.gz -o ${TMP}/${XNU}.tgz

cd ./mfiutil
tar xzf ${TMP}/${LU}.tgz -C .
tar xzf ${TMP}/${XNU}.tgz -C .
mkdir ./sys
mkdir ./machine
mkdir -p ./dev/mfi
mkdir -p ./cam/scsi

mkdir ./libkern
ln -s ${TMP}/sys/param.h ${TMP}/sys/_null.h \
	./sys/
#${TMP}/sys/_stdint.h
ln -s ${TMP}/scsi/scsi_all.h ./cam/scsi/
ln -s ${TMP}/mfi/mfireg.h ${TMP}/mfi/mfi_ioctl.h ./dev/mfi/
mkdir ../dev && cp -r ./dev/mfi ../dev/

#ln -s ${INC}/i386/param.h ./sys/
ln -s ${INC}/stdint.h ./sys/_stdint.h
ln -s ${INC}/sys/syslimits.h ./sys/limits.h
# Apple does not appear to provide llvm-config
ln -s /System/Library/Frameworks/Kernel.framework/Versions/A/Headers/stdarg.h ./machine/
ln -s ./${LU}/libutil.h .
ln -s ${PWD}/${XNU}/bsd/sys/linker_set.h ./sys/
ln -s ${PWD}/${XNU}/libkern/libkern/kernel_mach_header.h ./libkern/

cat > patch-mfiutil.h << EOF
--- mfiutil.h.orig	2013-03-28 12:06:19.000000000 +0400
+++ mfiutil.h	2013-03-28 12:31:25.000000000 +0400
@@ -33,35 +33,47 @@
 #define	__MFIUTIL_H__
 
 #include <sys/cdefs.h>
+
+typedef struct mach_header kernel_mach_header_t;
 #include <sys/linker_set.h>
 
 #include <dev/mfi/mfireg.h>
 
-/* 4.x compat */
+/* OS X compat */
 #ifndef SET_DECLARE
 
-/* <sys/cdefs.h> */
-#define	__used
-#define	__section(x)	__attribute__((__section__(x)))
-
 /* <sys/linker_set.h> */
-#undef __MAKE_SET
 #undef DATA_SET
 
-#define __MAKE_SET(set, sym)						\\
-	static void const * const __set_##set##_sym_##sym 		\\
-	__section("set_" #set) __used = &sym
-
-#define DATA_SET(set, sym)	__MAKE_SET(set, sym)
+#define DATA_SET(set, sym)	__LINKER_MAKE_SET(set, sym)
 
 #define SET_DECLARE(set, ptype)						\\
-	extern ptype *__CONCAT(__start_set_,set);			\\
-	extern ptype *__CONCAT(__stop_set_,set)
+	char set[] = __LS_VA_STRINGIFY(set)
+
+static __inline void **
+__linker_set_object_begin_slide(const char *_set)
+{
+        void *_set_begin;
+        unsigned long _size;
+
+        _set_begin = getsectiondata(&_mh_execute_header, SEG_DATA, _set, &_size);
+        return( (void **) _set_begin );
+}
+static __inline void **
+__linker_set_object_limit_slide(const char *_set)
+{
+        void *_set_begin;
+        unsigned long _size;
+
+        _set_begin = getsectiondata(&_mh_execute_header, SEG_DATA, _set, &_size);
+
+        return ((void **) ((uintptr_t) _set_begin + _size));
+}
 
 #define SET_BEGIN(set)							\\
-	(&__CONCAT(__start_set_,set))
+	__linker_set_object_begin_slide(set)
 #define SET_LIMIT(set)							\\
-	(&__CONCAT(__stop_set_,set))
+	__linker_set_object_limit_slide(set)
 
 #define	SET_FOREACH(pvar, set)						\\
 	for (pvar = SET_BEGIN(set); pvar < SET_LIMIT(set); pvar++)
@@ -97,7 +109,7 @@ struct mfiutil_command {
 	int (*handler)(int ac, char **av);
 };
 
-#define	MFI_DATASET(name)	mfiutil_ ## name ## _table
+#define	MFI_DATASET(name)	mfu_ ## name ## _tbl
 
 #define	MFI_COMMAND(set, name, function)				\\
 	static struct mfiutil_command function ## _mfiutil_command =	\\
EOF
patch < patch-mfiutil.h

#CC=gcc
bsdmake
