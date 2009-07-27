# Process this file with autoconf to produce a configure script.
AC_INIT(wy60, 1.1.2, markus+wy60@wy60.gutschke.com)
AM_INIT_AUTOMAKE(wy60, 1.1.2, markus+wy60@wy60.gutschke.com)
AM_CONFIG_HEADER(config.h)

# Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S

# Checks for libraries.
AC_CHECK_FUNCS(setupterm, [],
               [AC_CHECK_LIB(curses, setupterm, LIBS="-lcurses $LIBS",
                  [AC_CHECK_LIB(ncurses, setupterm, LIBS="-lncurses $LIBS")])])

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([curses.h errno.h fcntl.h grp.h ncurses.h ncurses/term.h     \
                  netinet/in.h pty.h setjmp.h signal.h stdarg.h stdio.h       \
                  stdlib.h string.h strings.h sys/filio.h sys/ioctl.h         \
                  sys/poll.h sys/time.h sys/ttydefaults.h syslog.h term.h     \
                  termios.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_HEADER_TIME

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_FORK
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_CHECK_FUNCS([atexit cfmakeraw dup2 gettimeofday memmove memset putenv      \
                strcasecmp strchr strdup strrchr])
AC_TRY_LINK([#ifndef _GNU_SOURCE
             #define _GNU_SOURCE
             #endif
             #include <stdlib.h>],
            [getpt();],
            [AC_DEFINE(HAVE_GETPT, 1,
                       Define to 1 if you have UNIX98 style PTYs)],
            [AC_DEFINE(HAVE_GETPT, 0)])
AC_TRY_LINK([#ifndef _GNU_SOURCE
             #define _GNU_SOURCE
             #endif
             #include <stdlib.h>],
            [grantpt(0);],
            [AC_DEFINE(HAVE_GRANTPT, 1,
                       Define to 1 if you have UNIX98 style PTYs)],
            [AC_DEFINE(HAVE_GRANTPT, 0)])
AC_TRY_COMPILE([#include <signal.h>],
               [int i = SIGSYS; ],
               [AC_DEFINE(HAVE_SIGSYS, 1,
                          Define to 1 if you have the SIGSYS signal)],
               [AC_DEFINE(HAVE_SIGSYS, 0)])
AC_TRY_COMPILE([#include <sys/ioctl.h>],
               [ioctl(0, TIOCSCTTY, 0);],
               [AC_DEFINE(HAVE_TIOCSCTTY, 1,
                       Define to 1 if you have the BSD style TIOCSCTTY ioctl)],
               [AC_DEFINE(HAVE_TIOCSCTTY, 0)])
#endif


AH_VERBATIM([_GNU_SOURCE],
[/* Enable GNU extensions on systems that have them. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT