dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

dnl ================================================================
dnl Package version (i.e. the pretty number that users see)
dnl ================================================================
m4_define([rig_version], 2)

dnl ================================================================
dnl Interface version details for libtool
dnl ================================================================
# NB: I didn't copy the widely used glib/gtk style of dealing with libtool
# versioning since I found the different (compared to libtool's documentation)
# ideas for what the interface age should represent; and the addition of a
# binary age confusing. I couldn't find any documentation/mailing list archives
# etc explaing the ideas nor any code that makes use of those values. They also
# seem to go against the advice of not basing the libtool versioning on package
# versioning.
#
# For reference on how the various numbers should be updated at release time
# these rules are adapted from the libtool info pages:
#
#  1. Update the version information only immediately before a public
#     release of your software.
#
#  2. If the library source code has changed at all since the last
#     update, then increment REVISION (`C:R:A' becomes `C:r+1:A').
#
#  3. If any interfaces have been added, removed, or changed since the
#     last update, increment CURRENT, and set REVISION to 0.
#
#  4. If any interfaces have been added since the last public release,
#     then increment AGE.
#
#  5. If any interfaces have been removed since the last public release,
#     then set AGE to 0.
m4_define([rig_lt_current], 0)
m4_define([rig_lt_revision], 0)
m4_define([rig_lt_age], 0)
# We do also tell libtool the pretty version also:
m4_define([rig_lt_release], [rig_version])


dnl ================================================================
dnl Source code release status
dnl ================================================================
# Finally we explicitly track when we are building development source
# from Git vs building source corresponding to a release. As with the
# libtool version info we don't automatically derive this from the
# pretty version number because we want to test the results of
# updating the version number in advance of a release.
m4_define([rig_release_status], [git])


dnl ================================================================
dnl Required versions for dependencies
dnl ================================================================
m4_define([glib_req_version],           [2.26.0])
m4_define([gi_req_version],             [0.9.5])
m4_define([gtk_doc_req_version],        [1.13])


AC_INIT(rig, [rig_version])
AC_CONFIG_SRCDIR(rut/rut.c)
AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/autotools])
AC_CONFIG_HEADERS(config.h)

# Save this value here, since automake will set cflags later and we
# want to know if the user specified custom cflags or not.
cflags_set=${CFLAGS+set}

AM_INIT_AUTOMAKE([1.11 foreign -Wno-portability no-define no-dist-gzip
                  dist-bzip2])
AM_SILENT_RULES([yes])

dnl ================================================================
dnl More version number stuff
dnl ================================================================
RIG_VERSION=rig_version

AC_SUBST(RIG_VERSION)


dnl ================================================================
dnl libtool versioning
dnl ================================================================
RIG_LT_CURRENT=rig_lt_current
RIG_LT_REVISION=rig_lt_revision
RIG_LT_AGE=rig_lt_age
RIG_LT_RELEASE=rig_lt_release
AC_SUBST(RIG_LT_CURRENT)
AC_SUBST(RIG_LT_REVISION)
AC_SUBST(RIG_LT_AGE)
AC_SUBST(RIG_LT_RELEASE)


dnl ================================================================
dnl Export the source code release status
dnl ================================================================
AC_SUBST([RIG_RELEASE_STATUS], [rig_release_status])


dnl ================================================================
dnl See what platform we are building for
dnl ================================================================
AC_CANONICAL_HOST
AC_MSG_CHECKING([if building for some Win32 platform])
AS_CASE([$host],
        [*-*-mingw*|*-*-cygwin*],
        [
          RIG_EXTRA_LDFLAGS="$RIG_EXTRA_LDFLAGS -no-undefined"
          platform_win32=yes
        ],

        [platform_win32=no]
)
AC_MSG_RESULT([$platform_win32])
AM_CONDITIONAL(OS_WIN32, [test "$platform_win32" = "yes"])

dnl ================================================================
dnl Handle extra configure options
dnl ================================================================

m4_define([default], [m4_if(rig_release_status, [git], [yes], [no])])
AC_ARG_ENABLE(
  [debug],
  [AC_HELP_STRING([--enable-debug=@<:@no/yes@:>@], [Control debugging level @<:@default=]default[@:>@])],
  [],
  enable_debug=default
)
AS_CASE(
  [$enable_debug],
  [yes],
  [
    test "$cflags_set" = set || CFLAGS="$CFLAGS -g -O0"
    RIG_EXTRA_CPPFLAGS="$RIG_EXTRA_CPPFLAGS -DRIG_ENABLE_DEBUG"
  ],
  [no],
  [
    RIG_EXTRA_CPPFLAGS="$RIG_EXTRA_CPPFLAGS -DRIG_ENABLE_DEBUG -DG_DISABLE_CHECKS -DG_DISABLE_CAST_CHECKS"
  ],
  [AC_MSG_ERROR([Unknown argument for --enable-debug])]
)

AC_ARG_ENABLE(
  [refcount_debug],
  [AC_HELP_STRING([--enable-refcount-debug=@<:@no/yes@:>@],
                  [Enable tracking refcounts @<:@default=]default[@:>@])],
  [enable_refcount_debug="$enableval"],
  [enable_refcount_debug="$enable_debug"]
)
AS_CASE(
  [$enable_refcount_debug],
  [yes],
  [
    AC_DEFINE([RUT_ENABLE_REFCOUNT_DEBUG], [1],
              [Define to enable refcount debugging])
  ],
  [no], [],
  [AC_MSG_ERROR([Unknown argument for --enable-refcount-debug])]
)

AS_IF([test "x$enable_refcount_debug" = "xyes"],
      [AC_CHECK_HEADER([execinfo.h], [have_backtrace=yes], [have_backtrace=no])
       AC_CHECK_FUNC([backtrace], [], [have_backtrace=no])
       AC_CHECK_FUNC([backtrace_symbols], [], [have_backtrace=no])],
      [have_backtrace=no])

AS_IF([test "x$have_backtrace" = "xyes"],
      [AC_DEFINE([RUT_ENABLE_BACKTRACE],
                 [1],
                 [Define if backtracing is available])])

# use strict compiler flags only when building from git; the rules for
# distcheck will take care of turning this on when making a release
m4_define([default], [m4_if(rig_release_status, [git], [yes], [no])])
AC_ARG_ENABLE(
  [maintainer-flags],
  [AC_HELP_STRING([--enable-maintainer-flags=@<:@no/yes/error@:>@], [Use strict compiler flags @<:@default=]default[@:>@])],
  [],
  enable_maintainer_flags=default
)

MAINTAINER_COMPILER_FLAGS="-Wall -Wcast-align -Wuninitialized
                           -Wno-strict-aliasing -Wempty-body -Wformat
                           -Wformat-security -Winit-self
                           -Wdeclaration-after-statement
                           -Wpointer-arith"

AS_CASE(
  [$enable_maintainer_flags],
  [yes],
  [
    AS_COMPILER_FLAGS([MAINTAINER_CFLAGS], [$MAINTAINER_COMPILER_FLAGS])
  ],
  [no],
  [
  ],
  [error],
  [
    MAINTAINER_COMPILER_FLAGS="$MAINTAINER_COMPILER_FLAGS -Werror"
    AS_COMPILER_FLAGS([MAINTAINER_CFLAGS], [$MAINTAINER_COMPILER_FLAGS])
  ],
  [*],
  [AC_MSG_ERROR([Invalid option for --enable-maintainer-flags])]
)

# strip leading spaces
RIG_EXTRA_CFLAGS="$RIG_EXTRA_CFLAGS ${MAINTAINER_CFLAGS/#  }"

AC_ARG_ENABLE(uninstalled-build,
              AC_HELP_STRING([--enable-uninstalled-build],
			     [Search for files in build directory as well]),
	      enable_uninstalled=yes,)
if test x"$enable_uninstalled" = xyes; then
	AC_DEFINE_UNQUOTED(
        SHARE_UNINSTALLED_DIR,"`pwd`/rig/data",[path to source data dir])
fi

AC_ARG_ENABLE(editor,
              AC_HELP_STRING([--disable-editor],
			     [Disable building the interactive editor]),
              [],
	      [enable_editor=yes])
AS_IF([test x"$enable_editor" = xyes],
      [
	AC_DEFINE(RIG_EDITOR_ENABLED, [1], [Whether to build the editor or not])
      ])
AM_CONDITIONAL(EDITOR_ENABLED, test "x$enable_editor" = "xyes")

dnl ================================================================
dnl Compiler stuff.
dnl ================================================================
AC_PROG_CC
AC_PROG_CPP
AM_PROG_CC_C_O
AC_ISC_POSIX
AC_C_CONST
AC_PROG_OBJC

dnl ================================================================
dnl Libtool stuff.
dnl ================================================================
dnl AC_PROG_LIBTOOL
dnl LIBTOOL="$LIBTOOL --preserve-dup-deps"
LT_PREREQ([2.2.6])
LT_INIT([disable-static])

dnl ================================================================
dnl Check for dependency packages.
dnl ================================================================

dnl     ============================================================
dnl     Check glib dependencies
dnl     ============================================================
AM_PATH_GLIB_2_0([glib_req_version],
                 [have_glib=yes], [have_glib=no],
                 [gobject gthread gmodule-no-export])
AS_IF([test "x$have_glib" = "xno"], AC_MSG_ERROR([glib-2.0 is required]))

RIG_PKG_REQUIRES="$RIG_PKG_REQUIRES glib-2.0 gio-2.0 cogl2 cogl-pango2 avahi-glib avahi-client avahi-core libprotobuf-c cogl-gst"

dnl Check whether we are building for Android
AC_CHECK_DECL([__ANDROID__], [have_android=yes], [have_android=no])
AM_CONDITIONAL([HAVE_ANDROID], [test "x$have_android" = "xyes"])

dnl Add the build flags for android_native_app_glue
AS_IF([test "x$have_android" = "xyes"],
      [ANDROID_CHECK_MODULE([ANDROID_NATIVE_APP_GLUE],
                            [android/native_app_glue],
                            [android_native_app_glue])
      ANDROID_CHECK_MODULE([GLIB_ANDROID],
                           [glib-android],
                           [glib-android])
      CFLAGS="$CFLAGS $ANDROID_NATIVE_APP_GLUE_CFLAGS $GLIB_ANDROID_CFLAGS"])

dnl Check whether we are building for OSX
AC_CHECK_DECL([__APPLE__], [have_osx=yes], [have_osx=no])
AM_CONDITIONAL([HAVE_OSX], [test "x$have_osx" = "xyes"])
AS_IF([test "x$have_osx" = "xyes"],
      [RIG_EXTRA_LDFLAGS="$RIG_EXTRA_LDFLAGS -lobjc -Wl,-framework,Cocoa"])

AC_ARG_ENABLE([gtk],
              AS_HELP_STRING([--enable-gtk],
                             [Use GTK/X11 for menus and dialog boxes]),
              [enable_gtk="$enableval"],
              [enable_gtk="auto"])
AS_IF([test "x$enable_gtk" = "xauto"],
      AS_IF([test "x$have_android" = "xno" -a "x$have_osx" = "xno"],
            [PKG_CHECK_MODULES([GTK],
                               [gtk+-x11-3.0],
                               [enable_gtk=yes],
                               [enable_gtk=no])],
            [enable_gtk=no]))
AS_CASE([$enable_gtk],
        [yes],
        [RIG_PKG_REQUIRES="$RIG_PKG_REQUIRES gtk+-x11-3.0"
         AC_DEFINE([USE_GTK], [1], [Defined if GTK should be used])
        ],
        [no], [],
        [AC_MSG_ERROR([Invalid value for --enable-gtk])])
AM_CONDITIONAL([HAVE_GTK], [test "x$enable_gtk" = "xyes"])

AC_SUBST(RIG_PKG_REQUIRES)
PKG_CHECK_MODULES(RIG_DEP, [$RIG_PKG_REQUIRES])

PKG_CHECK_MODULES([LIBCRYPTO],
                  [libcrypto],
                  [have_libcrypto=yes],
                  [have_libcrypto=no])
AM_CONDITIONAL([HAVE_LIBCRYPTO], [test "x$have_libcrypto" = "xyes"])

dnl ================================================================
dnl Misc program dependencies.
dnl ================================================================
AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PROG_INSTALL

dnl ================================================================
dnl GObject-Introspection check
dnl ================================================================
GOBJECT_INTROSPECTION_CHECK([gi_req_version])


dnl ================================================================
dnl Checks for header files.
dnl ================================================================
AC_HEADER_STDC
AC_CHECK_HEADERS(fcntl.h limits.h unistd.h signal.h)


dnl ================================================================
dnl Checks for library functions.
dnl ================================================================
AC_TYPE_SIGNAL
AC_CHECK_FUNCS(putenv strdup)


dnl ================================================================
dnl What needs to be substituted in other files
dnl ================================================================
AC_SUBST(RIG_EXTRA_CPPFLAGS)
AC_SUBST(RIG_EXTRA_CFLAGS)
AC_SUBST(RIG_EXTRA_CPPFLAGS)
AC_SUBST(RIG_EXTRA_LDFLAGS)
AC_SUBST(RIG_DEP_CFLAGS)
AC_SUBST(RIG_DEP_LIBS)


dnl ================================================================
dnl Dah Da!
dnl ================================================================
AC_OUTPUT(
Makefile
rut/Makefile
data/Makefile
data/Info.plist
rig/Makefile
rig/jni/rig-defines.h
tools/Makefile
)

echo ""
echo "Rig - $RIG_VERSION ($RIG_RELEASE_STATUS)"
echo ""
echo " • Editor: $enable_editor"
echo ""
echo " • Compiler options:"
echo "        Rig debug: ${enable_debug}"
echo "        Refcount debugging: ${enable_refcount_debug}"
echo "        Compiler flags: ${CFLAGS} ${RIG_EXTRA_CFLAGS}"
echo "        Preprocessor flags: ${CPPFLAGS} ${RIG_EXTRA_CPPFLAGS}"
echo "        Linker flags: ${LDFLAGS} ${RIG_EXTRA_LDFLAGS}"
echo ""
