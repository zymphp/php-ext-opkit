dnl
dnl config.m4 for extension opkit
dnl
dnl Author: Eno-CN <Eno_CN@qq.com>
dnl

PHP_ARG_ENABLE([opkit],
  [whether to enable opkit support],
  [AS_HELP_STRING([--enable-opkit],
    [Enable opkit support (default: yes)])],
  [yes])

if test "$PHP_OPKIT" != "no"; then
  dnl Always build as shared extension for Zend Extension compatibility
  ext_shared=yes

  dnl Check for compiler flag support to silence implicit-fallthrough warnings
  AX_CHECK_COMPILE_FLAG([-Wno-implicit-fallthrough], [
    PHP_OPKIT_CFLAGS="$PHP_OPKIT_CFLAGS -Wno-implicit-fallthrough"
  ],, [-Werror])

  PHP_ADD_INCLUDE([$ext_srcdir/src])

  PHP_NEW_EXTENSION([opkit], [ \
    src/opkit.c \
    src/opkit_module.c \
    src/opkit_compile.c \
    src/opkit_util_funcs.c \
    src/opkit_zend_persist_calc.c \
    src/opkit_zend_persist.c \
  ], [$ext_shared],, [$PHP_OPKIT_CFLAGS -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1],, [yes])

  PHP_ADD_EXTENSION_DEP([opkit], [phar], [true])
fi
