AC_PREREQ(2.59)
AC_INIT(sidc, 1.6, rissko@gmail.com)
AC_CONFIG_SRCDIR([sidc.c])
AC_CONFIG_HEADER([config.h])

AC_PROG_CC

if test "$GCC" = yes
then
  CFLAGS="$CFLAGS -Wall"
fi

AC_C_BIGENDIAN
AC_SYS_LARGEFILE
AC_SYS_LONG_FILE_NAMES
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h stdint.h stdlib.h string.h sys/ioctl.h unistd.h sys/time.h])

AC_C_INLINE
AC_STRUCT_TM

AC_FUNC_FORK
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_SETPGRP
AC_FUNC_STAT
AC_FUNC_VPRINTF
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset mkdir sqrt strchr strerror gettimeofday strdup])

AC_CHECK_FUNCS([strcasecmp rint])
AC_HEADER_TIME

AC_SEARCH_LIBS( sqrt, [m], , [
   echo
   echo 'ERROR: Cannot find math library.'
   echo
   exit 1
])

AC_SEARCH_LIBS([fftw_execute], [fftw3], , [
   echo
   echo 'ERROR: Cannot find fftw library.'
   echo 'Download and install fftw3 from www.fftw.org.'
   echo
   exit 1
])

AC_SEARCH_LIBS([snd_pcm_open], [asound],
    [AC_DEFINE( [ALSA], [1], [Use ALSA interface])
       echo
       echo 'Configured for ALSA'
       echo ] ,
    [AC_DEFINE( [OSS], [1], [Use OSS if ALSA not available])
       echo
       echo 'Configured for OSS'
       echo 
AC_CHECK_HEADERS([/usr/lib/oss/include/sys/soundcard.h],
          [echo
           echo 'Using the 4-Front OSS soundcard.h'
           echo])])

AC_CONFIG_FILES([
   Makefile
   ])

AC_OUTPUT
