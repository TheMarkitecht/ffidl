/*****************************************
 * ffidl
 *
 * A combination of libffi or ffcall, for foreign function
 * interface, and libdl, for dynamic library loading and
 * symbol listing,  packaged with hints from ::dll, and
 * exported to Tcl.
 *
 * Ffidl - Copyright (c) 1999 by Roger E Critchlow Jr,
 * Santa Fe, NM, USA, rec@elf.org
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the ``Software''), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED ``AS IS'', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL ROGER E CRITCHLOW JR BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Note that this distribution of Ffidl contains copies of libffi and
 * ffcall, each of which has its own Copyright notice and License.
 *
 */

/*
 * Changes since ffidl 0.5:
 *  - updates for 2005 versions of libffi & ffcall
 *  - TEA 3.2 buildsystem, testsuite
 *  - support for Tcl 8.4, Tcl_WideInt, TclpDlopen
 *  - support for Darwin PowerPC
 *  - fixes for 64bit (LP64)
 *  - callouts & callbacks are created/used relative to current namespace (for unqualified names)
 *  - addition of [ffidl::stubsymbol] for Tcl/Tk symbol resolution via stubs tables
 *  - callbacks can be called anytime, not just from inside callouts (using Tcl_BackgroundError to report errors)
 * These changes are under BSD License and are
 * Copyright (c) 2005, Daniel A. Steffen <das@users.sourceforge.net>
 *
 * Changes since ffidl 0.6:
 *  - Build support for Tcl 8.6. (by pooryorick)
 *
 * Changes since ffidl 0.6:
 *  - Updates for API changes in 2015 version of libffi.
 *  - Fixes for Tcl_WideInt.
 *  - Update build system to TEA 3.9.
 * These changes are under BSD License and are
 * Copyright (c) 2015, Patzschke + Rasp Software GmbH, Wiesbaden
 * Author: Adrián Medraño Calvo <amcalvo@prs.de>
 *
 * Changes since ffidl 0.7:
 *  - Support for LLP64 (Win64).
 *  - Support specifying callback's command prefix.
 *  - Fix usage of libffi's return value API.
 *  - Disable long double support if longer than double
 *  - ... see doc/ffidl.html
 * These changes are under BSD License and are
 * Copyright (c) 2018, Patzschke + Rasp Software GmbH, Wiesbaden
 * Author: Adrián Medraño Calvo <amcalvo@prs.de>
 *
 * Changes since ffidl 0.8:
 *  - Ported to Jim Tcl interpreter.
 * These changes are under BSD License and are
 * Copyright (c) 2019, Mark Hubbard, <TheMarkitecht@gmail.com>
 * 
 */

#include <ffidlConfig.h>

#include <jim.h>

/* these parts borrowed from Jim source.  this is done mainly to avoid modifying
the Jim interp at all right now.  in future they should be factored out from there
so they don't have to be duplicated here.  revisit.  */
/* since then, Jim's source is modified and some of these were removed from here. */
typedef void *ClientData; /* replace everywhere wtih Jim_ClientData instead? */
typedef struct Jim_EventLoop
{
    void *reserved1;
    void *reserved2;
    jim_wide timeEventNextId;   /* highest event id created, starting at 1 */
    time_t timeBase;
    int suppress_bgerror; /* bgerror returned break, so don't call it again */
} Jim_EventLoop;
/* */

#if defined(LOOKUP_TK_STUBS)
static const char *MyTkInitStubs(Jim_Interp *interp, char *version, int exact);
static void *tkStubsPtr, *tkPlatStubsPtr, *tkIntStubsPtr, *tkIntPlatStubsPtr, *tkIntXlibStubsPtr;
#else
#define tkStubsPtr NULL
#define tkPlatStubsPtr NULL
#define tkIntStubsPtr NULL
#define tkIntPlatStubsPtr NULL
#define tkIntXlibStubsPtr NULL
#endif

/*
 * Windows needs to know which symbols to export.  Unix does not.
 * BUILD_Ffidl should be undefined for Unix.
 */

#if defined(BUILD_Ffidl)
#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT
#endif /* BUILD_Ffidl */

#include <string.h>
#include <stdlib.h>


#define ckalloc(x) \
    ((void *) malloc((unsigned)(x)))
#define ckfree(x) \
    free((char *)(x))
#define ckrealloc(x,y) \
    ((void *) realloc((char *)(x), (unsigned)(y)))
/*
#define attemptckalloc(x) \
    ((void *) Tcl_AttemptAlloc((unsigned)(x)))
#define attemptckrealloc(x,y) \
    ((void *) Tcl_AttemptRealloc((char *)(x), (unsigned)(y)))
*/

#include <dstring.h>

#define CONST const

/*
 * We can use either
 * libffi, with a no strings attached license,
 * or ffcall, with a GPL license.
 */

#if defined USE_LIBFFI

/* workaround for ffi.h bug on certain platforms: */
#include <stddef.h>
#include <limits.h>
#if HAVE_LONG_LONG
#  ifndef LONG_LONG_MAX
#    if SIZEOF_LONG_LONG == 4
#        define LONG_LONG_MAX 2147483647
#    elif SIZEOF_LONG_LONG == 8
#        define LONG_LONG_MAX 9223372036854775807
#    endif
#  endif
#endif

/*
 * We use two defines from ffi.h:
 *  FFI_NATIVE_RAW_API which indicates support for raw ffi api.
 *  FFI_CLOSURES which indicates support for callbacks.
 * libffi-1.20 doesn't define the latter, so we default it.
 */
#include <ffi.h>

#define USE_LIBFFI_RAW_API FFI_NATIVE_RAW_API


#ifndef FFI_CLOSURES
#define HAVE_CLOSURES 0
#else
#define HAVE_CLOSURES FFI_CLOSURES
#endif

#if defined(HAVE_LONG_DOUBLE) && defined(HAVE_LONG_DOUBLE_WIDER)
/*
 * Cannot support wider long doubles because they don't fit in Jim_Obj.
 */
#  undef HAVE_LONG_DOUBLE
#endif

#define lib_type_void	&ffi_type_void
#define lib_type_uint8	&ffi_type_uint8
#define lib_type_sint8	&ffi_type_sint8
#define lib_type_uint16	&ffi_type_uint16
#define lib_type_sint16	&ffi_type_sint16
#define lib_type_uint32	&ffi_type_uint32
#define lib_type_sint32	&ffi_type_sint32
#define lib_type_uint64	&ffi_type_uint64
#define lib_type_sint64	&ffi_type_sint64
#define lib_type_float	&ffi_type_float
#define lib_type_double	&ffi_type_double
#define lib_type_longdouble	&ffi_type_longdouble
#define lib_type_pointer	&ffi_type_pointer

#define lib_type_schar	&ffi_type_schar
#define lib_type_uchar	&ffi_type_uchar
#define lib_type_ushort	&ffi_type_ushort
#define lib_type_sshort	&ffi_type_sshort
#define lib_type_uint	&ffi_type_uint
#define lib_type_sint	&ffi_type_sint
/* ffi_type_ulong & ffi_type_slong are always 64bit ! */
#if SIZEOF_LONG == 2
#define lib_type_ulong	&ffi_type_uint16
#define lib_type_slong	&ffi_type_sint16
#elif SIZEOF_LONG == 4
#define lib_type_ulong	&ffi_type_uint32
#define lib_type_slong	&ffi_type_sint32
#elif SIZEOF_LONG == 8
#define lib_type_ulong	&ffi_type_uint64
#define lib_type_slong	&ffi_type_sint64
#endif
#if HAVE_LONG_LONG
#if SIZEOF_LONG_LONG == 2
#define lib_type_ulonglong	&ffi_type_uint16
#define lib_type_slonglong	&ffi_type_sint16
#elif SIZEOF_LONG_LONG == 4
#define lib_type_ulonglong	&ffi_type_uint32
#define lib_type_slonglong	&ffi_type_sint32
#elif SIZEOF_LONG_LONG == 8
#define lib_type_ulonglong	&ffi_type_uint64
#define lib_type_slonglong	&ffi_type_sint64
#endif
#endif

#if defined(__CHAR_UNSIGNED__)
#define lib_type_char	&ffi_type_uint8
#else
#define lib_type_char	&ffi_type_sint8
#endif

#endif /* #if defined USE_LIBFFI */

#if defined USE_LIBFFCALL
#include <avcall.h>
#include <callback.h>

/* Compatibility for libffcall < 2.0 */
#if LIBFFCALL_VERSION < 0x0200
#define callback_t __TR_function
#define callback_function_t __VA_function
#endif

#define HAVE_CLOSURES 1
#undef HAVE_LONG_DOUBLE		/* no support in ffcall */

#define lib_type_void	__AVvoid
#define lib_type_char	__AVchar
#define lib_type_schar	__AVschar
#define lib_type_uchar	__AVuchar
#define lib_type_sshort	__AVshort
#define lib_type_ushort	__AVushort
#define lib_type_sint	__AVint
#define lib_type_uint	__AVuint
#define lib_type_slong	__AVlong
#define lib_type_ulong	__AVulong
#define lib_type_slonglong	__AVlonglong
#define lib_type_ulonglong	__AVulonglong
#define lib_type_float	__AVfloat
#define lib_type_double	__AVdouble
#define lib_type_pointer	__AVvoidp
#define lib_type_struct	__AVstruct

#define av_sint	av_int
#define av_slong	av_long
#define av_slonglong	av_longlong
#define av_sshort	av_short
#define av_start_sint	av_start_int
#define av_start_slong	av_start_long
#define av_start_slonglong	av_start_longlong
#define av_start_sshort	av_start_short
 
/* NB, abbreviated to the most usual, add cases as required */
#if SIZEOF_CHAR == 1
#define lib_type_uint8	lib_type_uchar
#define av_start_uint8	av_start_uchar
#define av_uint8	av_uchar
#define va_start_uint8	va_start_uchar
#define va_arg_uint8	va_arg_uchar
#define va_return_uint8	va_return_uchar
#define lib_type_sint8	lib_type_schar
#define av_start_sint8	av_start_schar
#define av_sint8	av_schar
#define va_start_sint8	va_start_schar
#define va_arg_sint8	va_arg_schar
#define va_return_sint8	va_return_schar
#else
#error "no 8 bit int"
#endif

#if SIZEOF_SHORT == 2
#define lib_type_uint16	lib_type_ushort
#define av_start_uint16	av_start_ushort
#define av_uint16	av_ushort
#define va_start_uint16	va_start_ushort
#define va_arg_uint16	va_arg_ushort
#define va_return_uint16	va_return_ushort
#define lib_type_sint16	lib_type_sshort
#define av_start_sint16	av_start_sshort
#define av_sint16	av_sshort
#define va_start_sint16	va_start_short
#define va_arg_sint16	va_arg_short
#define va_return_sint16	va_return_short
#else
#error "no 16 bit int"
#endif

#if SIZEOF_INT == 4
#define lib_type_uint32	lib_type_uint
#define av_start_uint32	av_start_uint
#define av_uint32	av_uint
#define va_start_uint32	va_start_uint
#define va_arg_uint32	va_arg_uint
#define va_return_uint32	va_return_uint
#define lib_type_sint32	lib_type_sint
#define av_start_sint32	av_start_sint
#define av_sint32	av_sint
#define va_start_sint32	va_start_int
#define va_arg_sint32	va_arg_int
#define va_return_sint32	va_return_int
#else
#error "no 32 bit int"
#endif

#if SIZEOF_LONG == 8
#define lib_type_uint64	lib_type_ulong
#define av_start_uint64	av_start_ulong
#define av_uint64	av_ulong
#define va_start_uint64	va_start_ulong
#define va_arg_uint64	va_arg_ulong
#define va_return_uint64	va_return_ulong
#define lib_type_sint64	lib_type_slong
#define av_start_sint64	av_start_slong
#define av_sint64	av_slong
#define va_start_sint64	va_start_long
#define va_arg_sint64	va_arg_long
#define va_return_sint64	va_return_long
#elif HAVE_LONG_LONG && SIZEOF_LONG_LONG == 8
#define lib_type_uint64	lib_type_ulonglong
#define av_start_uint64	av_start_ulonglong
#define av_uint64	av_ulonglong
#define va_start_uint64	va_start_ulonglong
#define va_arg_uint64	va_arg_ulonglong
#define va_return_uint64	va_return_ulonglong
#define lib_type_sint64	lib_type_slonglong
#define av_start_sint64	av_start_slonglong
#define av_sint64	av_slonglong
#define va_start_sint64	va_start_longlong
#define va_arg_sint64	va_arg_longlong
#define va_return_sint64	va_return_longlong
#endif

#endif	/* #if defined USE_LIBFFCALL */
/*
 * Turn callbacks off if they're not implemented
 */
#if defined USE_CALLBACKS
#if ! HAVE_CLOSURES
#undef USE_CALLBACKS
#endif
#endif

/*****************************************
 *				  
 * ffidlopen, ffidlsym, and ffidlclose abstractions
 * of dlopen(), dlsym(), and dlclose().
 */
#if defined(USE_TCL_DLOPEN)

typedef Jim_LoadHandle ffidl_LoadHandle;
typedef Jim_FSUnloadFileProc *ffidl_UnloadProc;

#elif defined(USE_TCL_LOADFILE)

typedef Jim_LoadHandle ffidl_LoadHandle;
typedef Jim_FSUnloadFileProc *ffidl_UnloadProc;

#else

typedef void *ffidl_LoadHandle;
typedef void *ffidl_UnloadProc;

#if defined(__WIN32__)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

#else

#ifndef NO_DLFCN_H
#include <dlfcn.h>

/*
 * In some systems, like SunOS 4.1.3, the RTLD_NOW flag isn't defined
 * and this argument to dlopen must always be 1.  The RTLD_GLOBAL
 * flag is needed on some systems (e.g. SCO and UnixWare) but doesn't
 * exist on others;  if it doesn't exist, set it to 0 so it has no effect.
 */

#ifndef RTLD_NOW
#   define RTLD_NOW 1
#endif

#ifndef RTLD_GLOBAL
#   define RTLD_GLOBAL 0
#endif
#endif	/* NO_DLFCN_H */
#endif	/* __WIN32__ */
#endif	/* USE_TCL_DLOPEN */

enum ffidl_load_binding {
  FFIDL_LOAD_BINDING_NONE,
  FFIDL_LOAD_BINDING_NOW,
  FFIDL_LOAD_BINDING_LAZY
};

enum ffidl_load_visibility {
  FFIDL_LOAD_VISIBILITY_NONE,
  FFIDL_LOAD_VISIBILITY_LOCAL,
  FFIDL_LOAD_VISIBILITY_GLOBAL
};

struct ffidl_load_flags {
  enum ffidl_load_binding binding;
  enum ffidl_load_visibility visibility;
};
typedef struct ffidl_load_flags ffidl_load_flags;

/*****************************************
 *				  
 * Functions exported from this file.
 */

EXTERN void *ffidl_pointer_pun (void *p);
/* this function's name is based on the library's actual filename.  Jim requires that. */
EXTERN int   Jim_FfidlJimInit (Jim_Interp * interp);

/*****************************************
 *
 * Definitions.
 */
/*
 * values for ffidl_type.type
 */
enum ffidl_typecode {
  FFIDL_VOID		=  0,
    FFIDL_INT		=  1,
    FFIDL_FLOAT		=  2,
    FFIDL_DOUBLE	=  3,
#if HAVE_LONG_DOUBLE
    FFIDL_LONGDOUBLE	=  4,
#endif
    FFIDL_UINT8		=  5,
    FFIDL_SINT8		=  6,
    FFIDL_UINT16	=  7,
    FFIDL_SINT16	=  8,
    FFIDL_UINT32	=  9,
    FFIDL_SINT32	= 10,
    FFIDL_UINT64	= 11,
    FFIDL_SINT64	= 12,
    FFIDL_STRUCT	= 13,
    FFIDL_PTR		= 14,	/* integer value pointer */
    FFIDL_PTR_BYTE	= 15,	/* byte array pointer */
    FFIDL_PTR_UTF8	= 16,	/* UTF-8 string pointer */
    FFIDL_PTR_UTF16	= 17,	/* UTF-16 string pointer */
    FFIDL_PTR_VAR	= 18,	/* byte array in variable */
    FFIDL_PTR_OBJ	= 19,	/* Jim_Obj pointer */
    FFIDL_PTR_PROC	= 20,	/* Pointer to Tcl proc */

/*
 * aliases for unsized type names
 */
#if defined(__CHAR_UNSIGNED__)
    FFIDL_CHAR	= FFIDL_UINT8,
#else
    FFIDL_CHAR	= FFIDL_SINT8,
#endif

    FFIDL_SCHAR	= FFIDL_SINT8,
    FFIDL_UCHAR	= FFIDL_UINT8,

#if SIZEOF_SHORT == 2
    FFIDL_USHORT	= FFIDL_UINT16,
    FFIDL_SSHORT	= FFIDL_SINT16,
#elif SIZEOF_SHORT == 4
    FFIDL_USHORT	= FFIDL_UINT32,
    FFIDL_SSHORT	= FFIDL_SINT32,
#elif SIZEOF_SHORT == 8
    FFIDL_USHORT	= FFIDL_UINT64,
    FFIDL_SSHORT	= FFIDL_SINT64,
#else
#error "no short type"
#endif

#if SIZEOF_INT == 2
    FFIDL_UINT	= FFIDL_UINT16,
    FFIDL_SINT	= FFIDL_SINT16,
#elif SIZEOF_INT == 4
    FFIDL_UINT	= FFIDL_UINT32,
    FFIDL_SINT	= FFIDL_SINT32,
#elif SIZEOF_INT == 8
    FFIDL_UINT	= FFIDL_UINT64,
    FFIDL_SINT	= FFIDL_SINT64,
#else
#error "no int type"
#endif

#if SIZEOF_LONG == 2
    FFIDL_ULONG	= FFIDL_UINT16,
    FFIDL_SLONG	= FFIDL_SINT16,
#elif SIZEOF_LONG == 4
    FFIDL_ULONG	= FFIDL_UINT32,
    FFIDL_SLONG	= FFIDL_SINT32,
#elif SIZEOF_LONG == 8
    FFIDL_ULONG	= FFIDL_UINT64,
    FFIDL_SLONG	= FFIDL_SINT64,
#else
#error "no long type"
#endif

#if HAVE_LONG_LONG
#if SIZEOF_LONG_LONG == 2
    FFIDL_ULONGLONG	= FFIDL_UINT16,
    FFIDL_SLONGLONG	= FFIDL_SINT16
#elif SIZEOF_LONG_LONG == 4
    FFIDL_ULONGLONG	= FFIDL_UINT32,
    FFIDL_SLONGLONG	= FFIDL_SINT32
#elif SIZEOF_LONG_LONG == 8
    FFIDL_ULONGLONG	= FFIDL_UINT64,
    FFIDL_SLONGLONG	= FFIDL_SINT64
#else
#error "no long long type"
#endif
#endif
};

/*
 * Once more through, decide the alignment and C types
 * for the sized ints
 */

#define ALIGNOF_INT8	1
#define UINT8_T		unsigned char
#define SINT8_T		signed char

#if SIZEOF_SHORT == 2
#define ALIGNOF_INT16	ALIGNOF_SHORT
#define UINT16_T	unsigned short
#define SINT16_T	signed short
#elif SIZEOF_INT == 2
#define ALIGNOF_INT16	ALIGNOF_INT
#define UINT16_T	unsigned int
#define SINT16_T	signed int
#elif SIZEOF_LONG == 2
#define ALIGNOF_INT16	ALIGNOF_LONG
#define UINT16_T	unsigned long
#define SINT16_T	signed long
#else
#error "no 16 bit int"
#endif

#if SIZEOF_SHORT == 4
#define ALIGNOF_INT32	ALIGNOF_SHORT
#define UINT32_T	unsigned short
#define SINT32_T	signed short
#elif SIZEOF_INT == 4
#define ALIGNOF_INT32	ALIGNOF_INT
#define UINT32_T	unsigned int
#define SINT32_T	signed int
#elif SIZEOF_LONG == 4
#define ALIGNOF_INT32	ALIGNOF_LONG
#define UINT32_T	unsigned long
#define SINT32_T	signed long
#else
#error "no 32 bit int"
#endif

#if SIZEOF_SHORT == 8
#define ALIGNOF_INT64	ALIGNOF_SHORT
#define UINT64_T	unsigned short
#define SINT64_T	signed short
#elif SIZEOF_INT == 8
#define ALIGNOF_INT64	ALIGNOF_INT
#define UINT64_T	unsigned int
#define SINT64_T	signed int
#elif SIZEOF_LONG == 8
#define ALIGNOF_INT64	ALIGNOF_LONG
#define UINT64_T	unsigned long
#define SINT64_T	signed long
#elif HAVE_LONG_LONG && SIZEOF_LONG_LONG == 8
#define ALIGNOF_INT64	ALIGNOF_LONG_LONG
#define UINT64_T	unsigned long long
#define SINT64_T	signed long long
#endif

#if defined(ALIGNOF_INT64)
#define HAVE_INT64	1
#endif

#if defined(HAVE_INT64)
#  if defined(TCL_WIDE_INT_IS_LONG)
#    define HAVE_WIDE_INT		0
#    define Ffidl_NewInt64Obj		Jim_NewLongObj
#    define Ffidl_GetInt64FromObj	Jim_GetLong
#    define Ffidl_Int64			long
#  else
#    define HAVE_WIDE_INT		1
#    define Ffidl_NewInt64Obj		Jim_NewWideIntObj
#    define Ffidl_GetInt64FromObj	Jim_GetWideIntFromObj
#    define Ffidl_Int64			Jim_WideInt
#  endif
#endif

/*
 * values for ffidl_type.class
 */
#define FFIDL_ARG		0x001	/* type parser in argument context */
#define FFIDL_RET		0x002	/* type parser in return context */
#define FFIDL_ELT		0x004	/* type parser in element context */
#define FFIDL_CBARG		0x008	/* type parser in callback argument context */
#define FFIDL_CBRET		0x010	/* type parser in callback return context */
#define FFIDL_ALL		(FFIDL_ARG|FFIDL_RET|FFIDL_ELT|FFIDL_CBARG|FFIDL_CBRET)
#define FFIDL_ARGRET		(FFIDL_ARG|FFIDL_RET)
#define FFIDL_GETINT		0x020	/* arg needs an int value */
#define FFIDL_GETDOUBLE		0x040	/* arg needs a double value */
#define FFIDL_GETBYTES		0x080	/* arg needs a bytearray value */
#define FFIDL_STATIC_TYPE	0x100	/* do not free this type */
#define FFIDL_GETWIDEINT	0x200	/* arg needs a wideInt value */

/*
 * Tcl object type used for representing pointers within Tcl.
 *
 * We wrap an existing "expr"-compatible Jim_ObjType, in order to easily support
 * pointer arithmetic and formatting withing Tcl.  The size of the Jim_ObjType
 * needs to match the pointer size of the platform: long on LP64, Jim_WideInt on
 * LLP64 (e.g. WIN64).
 */
#if SIZEOF_VOID_P == SIZEOF_LONG
#  define FFIDL_POINTER_IS_LONG 1
#elif SIZEOF_VOID_P == 8 && defined(HAVE_WIDE_INT)
#  define FFIDL_POINTER_IS_LONG 0
#else
#  error "pointer size not supported"
#endif

#if FFIDL_POINTER_IS_LONG
static Jim_Obj *Ffidl_NewPointerObj(Jim_Interp *interp, void *ptr) {
  return Jim_NewLongObj(interp, (long)ptr);
}
static int Ffidl_GetPointerFromObj(Jim_Interp *interp, Jim_Obj *obj, void **ptr) {
  int status;
  long l;
  status = Jim_GetLong(interp, obj, &l);
  *ptr = (void *)l;
  return status;
}
#  define FFIDL_GETPOINTER FFIDL_GETINT
#else
static Jim_Obj *Ffidl_NewPointerObj(Jim_Interp *interp, void *ptr) {
  return Jim_NewWideIntObj((Jim_WideInt)ptr);
}
static int Ffidl_GetPointerFromObj(Jim_Interp *interp, Jim_Obj *obj, void **ptr) {
  int status;
  Jim_WideInt w;
  status = Jim_GetWideIntFromObj(interp, obj, &w);
  *ptr = (void *)w;
  return status;
}
#  define FFIDL_GETPOINTER FFIDL_GETWIDEINT
#endif

/*****************************************
 * Due to an ancient libffi defect, not fixed due to compatibility concerns,
 * return values for types smaller than the architecture's register size need to
 * be treated specially.  This must only be done for return values of integral
 * types, and not for arguments.  The recommended approach is to use a buffer of
 * size ffi_arg for these values, disregarding the type size.
 *
 * The following macros select, for each architecture, the appropriate member of
 * ffidl_value to use for a return value of a particular type.
 */
#define FFIDL_FITS_INTO_ARG(type) FFI_SIZEOF_ARG <= SIZEOF_##type

#define FFIDL_RVALUE_TYPE_INT int
#if USE_LIBFFI && !FFIDL_FITS_INTO_ARG(INT)
#  define FFIDL_RVALUE_WIDENED_TYPE_INT ffi_arg
#else
#  define FFIDL_RVALUE_WIDENED_TYPE_INT FFIDL_RVALUE_TYPE_INT
#endif

#define FFIDL_RVALUE_TYPE_UINT8 UINT8_T
#if USE_LIBFFI && !FFIDL_FITS_INTO_ARG(UINT8)
#  define FFIDL_RVALUE_WIDENED_TYPE_UINT8 ffi_arg
#else
#  define FFIDL_RVALUE_WIDENED_TYPE_UINT8 FFIDL_RVALUE_TYPE_UINT8
#endif

#define FFIDL_RVALUE_TYPE_SINT8 SINT8_T
#if USE_LIBFFI && !FFIDL_FITS_INTO_ARG(SINT8)
#  define FFIDL_RVALUE_WIDENED_TYPE_SINT8 ffi_arg
#else
#  define FFIDL_RVALUE_WIDENED_TYPE_SINT8 FFIDL_RVALUE_TYPE_SINT8
#endif

#define FFIDL_RVALUE_TYPE_UINT16 UINT16_T
#if USE_LIBFFI && !FFIDL_FITS_INTO_ARG(UINT16)
#  define FFIDL_RVALUE_WIDENED_TYPE_UINT16 ffi_arg
#else
#  define FFIDL_RVALUE_WIDENED_TYPE_UINT16 FFIDL_RVALUE_TYPE_UINT16
#endif

#define FFIDL_RVALUE_TYPE_SINT16 SINT16_T
#if USE_LIBFFI && !FFIDL_FITS_INTO_ARG(SINT16)
#  define FFIDL_RVALUE_WIDENED_TYPE_SINT16 ffi_arg
#else
#  define FFIDL_RVALUE_WIDENED_TYPE_SINT16 FFIDL_RVALUE_TYPE_SINT16
#endif

#define FFIDL_RVALUE_TYPE_UINT32 UINT32_T
#if USE_LIBFFI && !FFIDL_FITS_INTO_ARG(UINT32)
#  define FFIDL_RVALUE_WIDENED_TYPE_UINT32 ffi_arg
#else
#  define FFIDL_RVALUE_WIDENED_TYPE_UINT32 FFIDL_RVALUE_TYPE_UINT32
#endif

#define FFIDL_RVALUE_TYPE_SINT32 SINT32_T
#if USE_LIBFFI && !FFIDL_FITS_INTO_ARG(SINT32)
#  define FFIDL_RVALUE_WIDENED_TYPE_SINT32 ffi_arg
#else
#  define FFIDL_RVALUE_WIDENED_TYPE_SINT32 FFIDL_RVALUE_TYPE_SINT32
#endif

#if HAVE_INT64
#  define FFIDL_RVALUE_TYPE_UINT64 UINT64_T
#  if USE_LIBFFI && !FFIDL_FITS_INTO_ARG(UINT64)
#    define FFIDL_RVALUE_WIDENED_TYPE_UINT64 ffi_arg
#  else
#    define FFIDL_RVALUE_WIDENED_TYPE_UINT64 FFIDL_RVALUE_TYPE_UINT64
#  endif

#  define FFIDL_RVALUE_TYPE_SINT64 SINT64_T
#  if USE_LIBFFI && !FFIDL_FITS_INTO_ARG(SINT64)
#    define FFIDL_RVALUE_WIDENED_TYPE_SINT64 ffi_arg
#  else
#    define FFIDL_RVALUE_WIDENED_TYPE_SINT64 FFIDL_RVALUE_TYPE_SINT64
#  endif
#endif	/* HAVE_INT64 */

/* Only integral types are affected, see above comment. */
#define FFIDL_RVALUE_TYPE_FLOAT float
#define FFIDL_RVALUE_WIDENED_TYPE_FLOAT FFIDL_RVALUE_TYPE_FLOAT
#define FFIDL_RVALUE_TYPE_DOUBLE double
#define FFIDL_RVALUE_WIDENED_TYPE_DOUBLE FFIDL_RVALUE_TYPE_DOUBLE
#if HAVE_LONG_DOUBLE
#  define FFIDL_RVALUE_TYPE_LONGDOUBLE long double
#  define FFIDL_RVALUE_WIDENED_TYPE_LONGDOUBLE FFIDL_RVALUE_TYPE_LONGDOUBLE
#endif

#define FFIDL_RVALUE_TYPE_PTR void *
#define FFIDL_RVALUE_WIDENED_TYPE_PTR FFIDL_RVALUE_TYPE_PTR

#define FFIDL_RVALUE_TYPE_STRUCT void *
#define FFIDL_RVALUE_WIDENED_TYPE_STRUCT FFIDL_RVALUE_TYPE_STRUCT

#define QUOTECONCAT(a, b) a ## b
#define CONCAT(a, b) QUOTECONCAT(a, b)
#define FFIDL_RVALUE_TYPE(type)   CONCAT(FFIDL_RVALUE_TYPE_,   type)
#define FFIDL_RVALUE_WIDENED_TYPE(type)  CONCAT(FFIDL_RVALUE_WIDENED_TYPE_,   type)
/* Retrieve and cast a widened return value to the return value. */
#define FFIDL_RVALUE_PEEK_UNWIDEN(type, rvalue) ((FFIDL_RVALUE_TYPE(type))*(FFIDL_RVALUE_WIDENED_TYPE(type) *)rvalue)
/* Cast a return value to the return widened return value, put it at dst. */
#define FFIDL_RVALUE_POKE_WIDENED(type, dst, src) (*(FFIDL_RVALUE_WIDENED_TYPE(type) *)dst = (FFIDL_RVALUE_TYPE(type))src)


/*****************************************
 *
 * Type definitions for ffidl.
 */
/*
 * forward declarations.
 */
typedef enum ffidl_typecode ffidl_typecode;
typedef union ffidl_value ffidl_value;
typedef struct ffidl_type ffidl_type;
typedef struct ffidl_client ffidl_client;
typedef struct ffidl_cif ffidl_cif;
typedef struct ffidl_callout ffidl_callout;
typedef struct ffidl_callback ffidl_callback;
typedef struct ffidl_closure ffidl_closure;
typedef struct ffidl_lib ffidl_lib;

/*
 * The ffidl_value structure contains a union used
 * for converting to/from Tcl type.
 */
union ffidl_value {
  int v_int;
  float v_float;
  double v_double;
#if HAVE_LONG_DOUBLE
  long double v_longdouble;
#endif
  UINT8_T v_uint8;
  SINT8_T v_sint8;
  UINT16_T v_uint16;
  SINT16_T v_sint16;
  UINT32_T v_uint32;
  SINT32_T v_sint32;
#if HAVE_INT64
  UINT64_T v_uint64;
  SINT64_T v_sint64;
#endif
  void *v_struct;
  void *v_pointer;
#if USE_LIBFFI
  ffi_arg v_ffi_arg;
#endif
};

/*
 * The ffidl_type structure contains a type code, a class,
 * the size of the type, the structure element alignment of
 * the class, and a pointer to the underlying ffi_type.
 */
struct ffidl_type {
   int refs;			/* Reference counting */
   size_t size;			/* Type's size */
   ffidl_typecode typecode;	/* Type identifier */
   unsigned short class;	/* Type's properties */
   unsigned short alignment;	/* Type's alignment */
   unsigned short nelts;	/* Number of elements */
   ffidl_type **elements;	/* Pointer to element types */
#if USE_LIBFFI
   ffi_type *lib_type;		/* libffi's type data */
#elif USE_LIBFFCALL
   enum __AVtype lib_type;	/* ffcall's type data */
   int splittable;
#endif
};

/*
 * The ffidl_client contains
 * a hashtable for ffidl-typedef definitions,
 * a hashtable for ffidl-callout definitions,
 * a hashtable for cif's keyed by signature,
 * a hashtable of libs loaded by ffidl-symbol,
 * a hashtable of callbacks keyed by proc name
 */
struct ffidl_client {
  Jim_HashTable types;
  Jim_HashTable cifs;
  Jim_HashTable callouts;
  Jim_HashTable libs;
  Jim_HashTable callbacks;
};

/*
 * The ffidl_cif structure contains an ffi_cif,
 * an array of ffidl_types used to construct the
 * cif and convert arguments, and an array of void*
 * used to pass converted arguments into ffi_call.
 */
struct ffidl_cif {
   int refs;		   /* Reference counting. */
   ffidl_client *client;   /* Backpointer to the ffidl_client. */
   int protocol;	   /* Calling convention. */
   ffidl_type *rtype;	   /* Type of return value. */
   int argc;		   /* Number of arguments. */
   ffidl_type **atypes;	   /* Type of each argument. */
#if USE_LIBFFI
   ffi_type **lib_atypes;	/* Pointer to storage area for libffi's internal
				 * argument types. */
   ffi_cif lib_cif;		/* Libffi's internal data. */
#endif
};

/*
 * The ffidl_callout contains a cif pointer,
 * a function address, the ffidl_client
 * which defined the callout, and a usage
 * string.
 */
struct ffidl_callout {
  ffidl_cif *cif;
  void (*fn)();
  ffidl_client *client;
  void *ret;		   /* Where to store the return value. */
  void **args;		   /* Where to store each of the arguments' values. */
  char *usage;
#if USE_LIBFFI && USE_LIBFFI_RAW_API
  int use_raw_api;		/* Whether to use libffi's raw API. */
#endif
};

#if USE_CALLBACKS
/*
 * The ffidl_closure contains a ffi_closure structure,
 * a Jim_Interp pointer, and a pointer to the callback binding.
 */
struct ffidl_closure {
#if USE_LIBFFI
   ffi_closure *lib_closure;	/* Points to the writtable part of the closure. */
   void *executable;		/* Points to the executable address of the closure. */
#elif USE_LIBFFCALL
   callback_t lib_closure;
#endif
};
/*
 * The ffidl_callback binds a ffidl_cif pointer to
 * a Tcl proc name, it defines the signature of the
 * c function call to the Tcl proc.
 */
struct ffidl_callback {
  ffidl_cif *cif;
  int cmdc;			/* Number of command prefix words. */
  Jim_Obj **cmdv;		/* Command prefix Jim_Objs. */
  Jim_Interp *interp;
  ffidl_closure closure;
#if USE_LIBFFI_RAW_API
  int use_raw_api;		/* Whether to use libffi's raw API. */
  ptrdiff_t *offsets;		/* Raw argument offsets. */
#endif
};
#endif

struct ffidl_lib {
  ffidl_LoadHandle loadHandle;
  ffidl_UnloadProc unloadProc;
};

/*****************************************
 *
 * Data defined in this file.
 * In addition to the version string above
 */

static const Jim_ObjType *ffidl_bytearray_ObjType;
static const Jim_ObjType *ffidl_int_ObjType;
#if HAVE_WIDE_INT
static const Jim_ObjType *ffidl_wideInt_ObjType;
#endif
static const Jim_ObjType *ffidl_double_ObjType;

/*
 * base types, the ffi base types and some additional bits.
 */
#define init_type(size,type,class,alignment,libtype) { 1/*refs*/, size, type, class|FFIDL_STATIC_TYPE, alignment, 0/*nelts*/, 0/*elements*/, libtype }

static ffidl_type ffidl_type_void = init_type(0, FFIDL_VOID, FFIDL_RET|FFIDL_CBRET, 0, lib_type_void);
static ffidl_type ffidl_type_char = init_type(SIZEOF_CHAR, FFIDL_CHAR, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_CHAR, lib_type_char);
static ffidl_type ffidl_type_schar = init_type(SIZEOF_CHAR, FFIDL_SCHAR, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_CHAR, lib_type_schar);
static ffidl_type ffidl_type_uchar = init_type(SIZEOF_CHAR, FFIDL_UCHAR, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_CHAR, lib_type_uchar);
static ffidl_type ffidl_type_sshort = init_type(SIZEOF_SHORT, FFIDL_SSHORT, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_SHORT, lib_type_sshort);
static ffidl_type ffidl_type_ushort = init_type(SIZEOF_SHORT, FFIDL_USHORT, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_SHORT, lib_type_ushort);
static ffidl_type ffidl_type_sint = init_type(SIZEOF_INT, FFIDL_SINT, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_INT, lib_type_sint);
static ffidl_type ffidl_type_uint = init_type(SIZEOF_INT, FFIDL_UINT, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_INT, lib_type_uint);
#if SIZEOF_LONG == 8
static ffidl_type ffidl_type_slong = init_type(SIZEOF_LONG, FFIDL_SLONG, FFIDL_ALL|FFIDL_GETWIDEINT, ALIGNOF_LONG, lib_type_slong);
static ffidl_type ffidl_type_ulong = init_type(SIZEOF_LONG, FFIDL_ULONG, FFIDL_ALL|FFIDL_GETWIDEINT, ALIGNOF_LONG, lib_type_ulong);
#else
static ffidl_type ffidl_type_slong = init_type(SIZEOF_LONG, FFIDL_SLONG, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_LONG, lib_type_slong);
static ffidl_type ffidl_type_ulong = init_type(SIZEOF_LONG, FFIDL_ULONG, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_LONG, lib_type_ulong);
#endif
#if HAVE_LONG_LONG
static ffidl_type ffidl_type_slonglong = init_type(SIZEOF_LONG_LONG, FFIDL_SLONGLONG, FFIDL_ALL|FFIDL_GETWIDEINT, ALIGNOF_LONG_LONG, lib_type_slonglong);
static ffidl_type ffidl_type_ulonglong = init_type(SIZEOF_LONG_LONG, FFIDL_ULONGLONG, FFIDL_ALL|FFIDL_GETWIDEINT, ALIGNOF_LONG_LONG, lib_type_ulonglong);
#endif
static ffidl_type ffidl_type_float = init_type(SIZEOF_FLOAT, FFIDL_FLOAT, FFIDL_ALL|FFIDL_GETDOUBLE, ALIGNOF_FLOAT, lib_type_float);
static ffidl_type ffidl_type_double = init_type(SIZEOF_DOUBLE, FFIDL_DOUBLE, FFIDL_ALL|FFIDL_GETDOUBLE, ALIGNOF_DOUBLE, lib_type_double);
#if HAVE_LONG_DOUBLE
static ffidl_type ffidl_type_longdouble = init_type(SIZEOF_LONG_DOUBLE, FFIDL_LONGDOUBLE, FFIDL_ALL|FFIDL_GETDOUBLE, ALIGNOF_LONG_DOUBLE, lib_type_longdouble);
#endif
static ffidl_type ffidl_type_sint8 = init_type(1, FFIDL_SINT8, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_INT8, lib_type_sint8);
static ffidl_type ffidl_type_uint8 = init_type(1, FFIDL_UINT8, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_INT8, lib_type_uint8);
static ffidl_type ffidl_type_sint16 = init_type(2, FFIDL_SINT16, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_INT16, lib_type_sint16);
static ffidl_type ffidl_type_uint16 = init_type(2, FFIDL_UINT16, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_INT16, lib_type_uint16);
static ffidl_type ffidl_type_sint32 = init_type(4, FFIDL_SINT32, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_INT32, lib_type_sint32);
static ffidl_type ffidl_type_uint32 = init_type(4, FFIDL_UINT32, FFIDL_ALL|FFIDL_GETINT, ALIGNOF_INT32, lib_type_uint32);
#if HAVE_INT64
static ffidl_type ffidl_type_sint64 = init_type(8, FFIDL_SINT64, FFIDL_ALL|FFIDL_GETWIDEINT, ALIGNOF_INT64, lib_type_sint64);
static ffidl_type ffidl_type_uint64 = init_type(8, FFIDL_UINT64, FFIDL_ALL|FFIDL_GETWIDEINT, ALIGNOF_INT64, lib_type_uint64);
#endif
static ffidl_type ffidl_type_pointer       = init_type(SIZEOF_VOID_P, FFIDL_PTR,       FFIDL_ALL|FFIDL_GETPOINTER,           ALIGNOF_VOID_P, lib_type_pointer);
static ffidl_type ffidl_type_pointer_obj   = init_type(SIZEOF_VOID_P, FFIDL_PTR_OBJ,   FFIDL_ARGRET|FFIDL_CBARG|FFIDL_CBRET, ALIGNOF_VOID_P, lib_type_pointer);
static ffidl_type ffidl_type_pointer_utf8  = init_type(SIZEOF_VOID_P, FFIDL_PTR_UTF8,  FFIDL_ARGRET|FFIDL_CBARG,             ALIGNOF_VOID_P, lib_type_pointer);
//static ffidl_type ffidl_type_pointer_utf16 = init_type(SIZEOF_VOID_P, FFIDL_PTR_UTF16, FFIDL_ARGRET|FFIDL_CBARG,             ALIGNOF_VOID_P, lib_type_pointer);
static ffidl_type ffidl_type_pointer_byte  = init_type(SIZEOF_VOID_P, FFIDL_PTR_BYTE,  FFIDL_ARG,                            ALIGNOF_VOID_P, lib_type_pointer);
static ffidl_type ffidl_type_pointer_var   = init_type(SIZEOF_VOID_P, FFIDL_PTR_VAR,   FFIDL_ARG,                            ALIGNOF_VOID_P, lib_type_pointer);
#if USE_CALLBACKS
static ffidl_type ffidl_type_pointer_proc = init_type(SIZEOF_VOID_P, FFIDL_PTR_PROC, FFIDL_ARG, ALIGNOF_VOID_P, lib_type_pointer);
#endif

/*****************************************
 *
 * Functions defined in this file.
 */

void AppendResult(Jim_Interp *interp, ...)
{
    /* implementation similar to Jim_AppendStrings() */

    va_list ap;
    va_start(ap, interp);
    while (1) {
        const char *s = va_arg(ap, const char *);
        if (s == NULL)
            break;
        Jim_AppendString(interp, interp->result, s, -1);
    }
    va_end(ap);
}

/*
 * Dynamic loading
 */
#if !defined(USE_TCL_DLOPEN) && !defined(USE_TCL_LOADFILE)
static int ffidlsymfallback(ffidl_LoadHandle handle,
			    const char *nativeSymbolName,
			    void **address,
			    char *error)
{
  int status = JIM_OK;
#if defined(__WIN32__)
  /*
   * Ack, what about data?  I guess they're not particular,
   * some windows headers declare data as dll import, eg
   * vc98/include/math.h: _CRTIMP extern double _HUGE;
   */
  *address = GetProcAddress(handle, nativeSymbolName);
  if (!*address) {
    unknown = "unknown error";
    status = JIM_ERR;
  }
#else
  dlerror();			/* clear any old error. */
  *address = dlsym(handle, nativeSymbolName);
  error = dlerror();
  if (error) {
    status = JIM_ERR;
  }
#endif /* __WIN32__ */
  return status;
}
#endif /* !USE_TCL_DLOPEN && !USE_TCL_LOADFILE */

static int ffidlsym(Jim_Interp *interp,
		    ffidl_LoadHandle handle,
		    Jim_Obj *symbolNameObj,
		    void **address)
{
  int status = JIM_OK;
  char *error = NULL;
  const char *symbolName = NULL;
  const char *nativeSymbolName = NULL;
  Jim_DString nds;

  symbolName = Jim_GetString(symbolNameObj, NULL);
  /* this line could not port to Jim: */
  /* nativeSymbolName = Jim_UtfToExternalDString(NULL, symbolName, -1, &nds); */
  nativeSymbolName = symbolName;
#if defined(USE_TCL_DLOPEN)
  *address = TclpFindSymbol(interp, (Jim_LoadHandle)handle, nativeSymbolName);
  if (!*address) {
    error = "TclpFindSymbol() failed";
    status = JIM_ERR;
  }
#elif defined(USE_TCL_LOADFILE)
  *address = Jim_FindSymbol(interp, (Jim_LoadHandle)handle, nativeSymbolName);
  if (!*address) {
    error = "Jim_FindSymbol() failed";
    status = JIM_ERR;
  }
#else
  status = ffidlsymfallback(handle, nativeSymbolName, address, error);
  if (status != JIM_OK) {
    /*
     * Some platforms still add an underscore to the beginning of symbol
     * names.  If we can't find a name without an underscore, try again
     * with the underscore.
     */
    char *newNativeSymbolName = NULL;
    Jim_DString uds;
    char *ignoreerror = NULL;

    Jim_DStringInit(&uds);
    Jim_DStringAppend(&uds, "_", 1);
    Jim_DStringAppend(&uds, Jim_DStringValue(&nds), Jim_DStringLength(&nds));
    newNativeSymbolName = Jim_DStringValue(&uds);

    status = ffidlsymfallback(handle, newNativeSymbolName, address, ignoreerror);

    Jim_DStringFree(&uds);
  }
#endif /* USE_TCL_{LOADFILE,DLOPEN} */

  Jim_DStringFree(&nds);

  if (error) {
    AppendResult(interp, "couldn't find symbol \"", symbolName, "\" : ", error, NULL);
  }

  return status;
}

static int ffidlopen(Jim_Interp *interp,
		     Jim_Obj *libNameObj,
		     ffidl_load_flags flags,
		     ffidl_LoadHandle *handle,
		     ffidl_UnloadProc *unload)
{
  int status = JIM_OK;
#if defined(USE_TCL_DLOPEN)
  if (flags.binding != FFIDL_LOAD_BINDING_NONE ||
      flags.visibility != FFIDL_LOAD_VISIBILITY_NONE) {
    char *libraryName = NULL;
    libraryName = Jim_GetString(libNameObj, NULL);
    AppendResult(interp, "couldn't load file \"", libraryName, "\" : ",
		     "loading flags are not supported with USE_TCL_DLOPEN configuration",
		     (char *) NULL);
    status = JIM_ERR;
  } else {
    status = TclpDlopen(interp, libNameObj, handle, unload);
  }
#elif defined(USE_TCL_LOADFILE)
  {
    int tclflags =
      (flags.visibility == FFIDL_LOAD_VISIBILITY_GLOBAL? TCL_LOAD_GLOBAL : 0) |
      (flags.binding == FFIDL_LOAD_BINDING_LAZY? TCL_LOAD_LAZY : 0);
    if (Jim_LoadFile(interp, libNameObj, NULL, tclflags, NULL, handle) != JIM_OK) {
      status = JIM_ERR;
    }
  }
  *unload = NULL;
#else
  Jim_DString ds;
  const char *libraryName = NULL;
  const char *nativeLibraryName = NULL;
  char *error = NULL;

  libraryName = Jim_GetString(libNameObj, NULL);
  /* this line could not port to Jim: */
  /* nativeLibraryName = Jim_UtfToExternalDString(NULL, libraryName, -1, &ds); */
  nativeLibraryName = libraryName;
  nativeLibraryName = strlen(nativeLibraryName) ? nativeLibraryName : NULL;

#if defined(__WIN32__)
  if (flags.binding != FFIDL_LOAD_BINDING_NONE ||
      flags.visibility != FFIDL_LOAD_VISIBILITY_NONE) {
    error = "loading flags are not supported under windows";
    status = JIM_ERR;
  } else {
    *handle = LoadLibraryA(nativeLibraryName);
    if (!*handle) {
      error = "unknown error";
    }
  }
#else
  {
    int dlflags =
      (flags.visibility == FFIDL_LOAD_VISIBILITY_LOCAL? RTLD_LOCAL : RTLD_GLOBAL) |
      (flags.binding == FFIDL_LOAD_BINDING_LAZY? RTLD_LAZY : RTLD_NOW);
    *handle = dlopen(nativeLibraryName, dlflags);
    /* dlopen returns NULL when it fails. */
    if (!*handle) {
      error = dlerror();
    }
  }
#endif

  if (*handle == NULL) {
    AppendResult(interp, "couldn't load file \"", libraryName, "\" : ",
		     error, (char *) NULL);
    status = JIM_ERR;
  } else {
    *unload = NULL;
  }

  Jim_DStringFree(&ds);
#endif

  return status;
}

static int ffidlclose(Jim_Interp *interp,
		      char *libraryName,
		      ffidl_LoadHandle handle,
		      ffidl_UnloadProc unload)
{
  int status = JIM_OK;
  const char *error = NULL;
#if defined(USE_TCL_DLOPEN)
  /* NOTE: no error reporting. */
  ((Jim_FSUnloadFileProc*)unload)((Jim_LoadHandle)handle);
#elif defined(USE_TCL_LOADFILE)
  status = Jim_FSUnloadFile(interp, (Jim_LoadHandle)handle);
  if (status != JIM_OK) {
    error = Jim_GetStringResult(interp);
  }
#else
#if defined(__WIN32__)
  if (!FreeLibrary(handle)) {
    status = JIM_ERR;
    error = "unknown error";
  }
#else
  if (dlclose(handle)) {
    status = JIM_ERR;
    error = dlerror();
  }
#endif
#endif
  if (status != JIM_OK) {
    AppendResult(interp, "couldn't unload lib \"", libraryName, "\": ",
		     error, (char *) NULL);
  }
  return status;
}


/*
 * hash table management
 */
/* define a hashtable entry */
static void entry_define(Jim_HashTable *table, const char *name, void *datum)
{
  Jim_AddHashEntry(table, name, datum);
}
/* lookup an existing entry */
static void *entry_lookup(Jim_HashTable *table, const char *name)
{
  Jim_HashEntry *entry = Jim_FindHashEntry(table,name);
  return entry ? Jim_GetHashEntryVal(entry) : NULL;
}
/* find an entry by it's hash value */
static Jim_HashEntry *entry_find(Jim_HashTable *table, void *datum)
{
  Jim_HashEntry *entry;
  Jim_HashTableIterator * i;
  i = Jim_GetHashTableIterator(table);
  while ((entry = Jim_NextHashEntry(i))) {
    if (Jim_GetHashEntryVal(entry) == datum)
      break;
  }
  Jim_Free(i);
  return entry;
}
/*
 * type management
 */
/* define a new type */
static void type_define(ffidl_client *client, const char *tname, ffidl_type *ttype)
{
  entry_define(&client->types,tname,(void*)ttype);
}
/* lookup an existing type */
static ffidl_type *type_lookup(ffidl_client *client, const char *tname)
{
  return entry_lookup(&client->types,tname);
}
/* find a type by it's ffidl_type */
/*
static Jim_HashEntry *type_find(ffidl_client *client, ffidl_type *type)
{
  return entry_find(&client->types,(void *)type);
}
*/

/* Determine correct binary formats */
#if defined WORDS_BIGENDIAN
#define FFIDL_WIDEINT_FORMAT	"W"
#define FFIDL_INT_FORMAT	"I"
#define FFIDL_SHORT_FORMAT	"S"
#else
#define FFIDL_WIDEINT_FORMAT	"w"
#define FFIDL_INT_FORMAT	"i"
#define FFIDL_SHORT_FORMAT	"s"
#endif

/* build a binary format string */
static int type_format(Jim_Interp *interp, ffidl_type *type, int *offset)
{
  int i;
  char buff[128];
  /* Handle void case. */
  if (type->size == 0) {
    Jim_SetEmptyResult(interp);
    return JIM_OK;
  }
  /* Insert alignment padding */
  while ((*offset % type->alignment) != 0) {
    AppendResult(interp, "x", NULL);
    *offset += 1;
  }
  switch (type->typecode) {
  case FFIDL_INT:
  case FFIDL_UINT8:
  case FFIDL_SINT8:
  case FFIDL_UINT16:
  case FFIDL_SINT16:
  case FFIDL_UINT32:
  case FFIDL_SINT32:
#if HAVE_INT64
  case FFIDL_UINT64:
  case FFIDL_SINT64:
#endif
  case FFIDL_PTR:
  case FFIDL_PTR_BYTE:
  case FFIDL_PTR_OBJ:
  case FFIDL_PTR_UTF8:
  case FFIDL_PTR_UTF16:
  case FFIDL_PTR_VAR:
  case FFIDL_PTR_PROC:
    switch (type->size) {
    case sizeof(Ffidl_Int64):
      *offset += 8;
      AppendResult(interp, FFIDL_WIDEINT_FORMAT, NULL);
      return JIM_OK;
    case sizeof(int):
      *offset += 4;
      AppendResult(interp, FFIDL_INT_FORMAT, NULL);
      return JIM_OK;
    case sizeof(short):
      *offset += 2;
      AppendResult(interp, FFIDL_SHORT_FORMAT, NULL);
      return JIM_OK;
    case sizeof(char):
      *offset += 1;
      AppendResult(interp, "c", NULL);
      return JIM_OK;
    default:
      *offset += type->size;
      sprintf(buff, "c%lu", (long)(type->size));
      AppendResult(interp, buff, NULL);
      return JIM_OK;
    }
  case FFIDL_FLOAT:
  case FFIDL_DOUBLE:
#if HAVE_LONG_DOUBLE
  case FFIDL_LONGDOUBLE:
#endif
    if (type->size == sizeof(double)) {
      *offset += 8;
      AppendResult(interp, "d", NULL);
      return JIM_OK;
    } else if (type->size == sizeof(float)) {
      *offset += 4;
      AppendResult(interp, "f", NULL);
      return JIM_OK;
    } else {
      *offset += type->size;
      sprintf(buff, "c%lu", (long)(type->size));
      AppendResult(interp, buff, NULL);
      return JIM_OK;
    }
  case FFIDL_STRUCT:
    for (i = 0; i < type->nelts; i += 1)
      if (type_format(interp, type->elements[i], offset) != JIM_OK)
	return JIM_ERR;
    /* Insert tail padding */
    while (*offset < type->size) {
      AppendResult(interp, "x", NULL);
      *offset += 1;
    }
    return JIM_OK;
  default:
    i = sprintf(buff, "cannot format ffidl_type: %d", type->typecode);
    Jim_SetResultString(interp, buff, i);
    return JIM_ERR;
  }
}
static ffidl_type *type_alloc(ffidl_client *client, int nelts)
{
  ffidl_type *newtype;
  newtype = (ffidl_type *)Jim_Alloc(sizeof(ffidl_type)
				  +nelts*sizeof(ffidl_type*)
#if USE_LIBFFI
				  +sizeof(ffi_type)+(nelts+1)*sizeof(ffi_type *)
#endif
				  );
  if (newtype == NULL) {
    return NULL;
  }
  /* initialize aggregate type */
  newtype->size = 0;
  newtype->typecode = FFIDL_STRUCT;
  newtype->class = FFIDL_ALL;
  newtype->alignment = 0;
  newtype->refs = 0;
  newtype->nelts = nelts;
  newtype->elements = (ffidl_type **)(newtype+1);
#if USE_LIBFFI
  newtype->lib_type = (ffi_type *)(newtype->elements+nelts);
  newtype->lib_type->size = 0;
  newtype->lib_type->alignment = 0;
  newtype->lib_type->type = FFI_TYPE_STRUCT;
  newtype->lib_type->elements = (ffi_type **)(newtype->lib_type+1);
#endif
  return newtype;
}
/* free a type */
static void type_free(ffidl_type *type)
{
  Jim_Free((void *)type);
}
/* maintain reference counts on type's */
static void type_inc_ref(ffidl_type *type)
{
  type->refs += 1;
}
static void type_dec_ref(ffidl_type *type)
{
  if (--type->refs == 0) {
    type_free(type);
  }
}
/* prep a type for use by the library */
static int type_prep(ffidl_type *type)
{
#if USE_LIBFFI
  ffi_cif cif;
  int i;
  for (i = 0; i < type->nelts; i += 1)
    type->lib_type->elements[i] = type->elements[i]->lib_type;
  type->lib_type->elements[i] = NULL;
  /* try out new type in a temporary cif, which should set size and alignment */
  if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, type->lib_type, NULL) != FFI_OK)
    return JIM_ERR;
  if (type->size != type->lib_type->size) {
    fprintf(stderr, "ffidl disagrees with libffi about aggregate size of type %u! %lu != %lu\n", type->typecode, (long)(type->size), (long)(type->lib_type->size));
  }
  if (type->alignment != type->lib_type->alignment) {
    fprintf(stderr, "ffidl disagrees with libffi about aggregate alignment of type  %u! %hu != %hu\n", type->typecode, type->alignment, type->lib_type->alignment);
  }
#elif USE_LIBFFCALL
  /* decide if the structure can be split into parts for register return */
  /* Determine whether a struct type is word-splittable, i.e. whether each of
   * its components fit into a register.
   * These macros are adapted from ffcall-1.6/avcall/avcall.h/av_word_splittable*()
   */
#define ffidl_word_splittable_1(slot1)  \
  (__ffidl_offset1(slot1)/sizeof(__avword) == (__ffidl_offset1(slot1)+__ffidl_sizeof(slot1)-1)/sizeof(__avword))
#define ffidl_word_splittable_2(slot1,slot2)  \
  ((__ffidl_offset1(slot1)/sizeof(__avword) == (__ffidl_offset1(slot1)+__ffidl_sizeof(slot1)-1)/sizeof(__avword)) \
   && (__ffidl_offset2(slot1,slot2)/sizeof(__avword) == (__ffidl_offset2(slot1,slot2)+__ffidl_sizeof(slot2)-1)/sizeof(__avword)) \
  )
#define ffidl_word_splittable_3(slot1,slot2,slot3)  \
  ((__ffidl_offset1(slot1)/sizeof(__avword) == (__ffidl_offset1(slot1)+__ffidl_sizeof(slot1)-1)/sizeof(__avword)) \
   && (__ffidl_offset2(slot1,slot2)/sizeof(__avword) == (__ffidl_offset2(slot1,slot2)+__ffidl_sizeof(slot2)-1)/sizeof(__avword)) \
   && (__ffidl_offset3(slot1,slot2,slot3)/sizeof(__avword) == (__ffidl_offset3(slot1,slot2,slot3)+__ffidl_sizeof(slot3)-1)/sizeof(__avword)) \
  )
#define ffidl_word_splittable_4(slot1,slot2,slot3,slot4)  \
  ((__ffidl_offset1(slot1)/sizeof(__avword) == (__ffidl_offset1(slot1)+__ffidl_sizeof(slot1)-1)/sizeof(__avword)) \
   && (__ffidl_offset2(slot1,slot2)/sizeof(__avword) == (__ffidl_offset2(slot1,slot2)+__ffidl_sizeof(slot2)-1)/sizeof(__avword)) \
   && (__ffidl_offset3(slot1,slot2,slot3)/sizeof(__avword) == (__ffidl_offset3(slot1,slot2,slot3)+__ffidl_sizeof(slot3)-1)/sizeof(__avword)) \
   && (__ffidl_offset4(slot1,slot2,slot3,slot4)/sizeof(__avword) == (__ffidl_offset4(slot1,slot2,slot3,slot4)+__ffidl_sizeof(slot4)-1)/sizeof(__avword)) \
  )
#define __ffidl_offset1(slot1)  \
  0
#define __ffidl_offset2(slot1,slot2)  \
  ((__ffidl_offset1(slot1)+__ffidl_sizeof(slot1)+__ffidl_alignof(slot2)-1) & -(long)__ffidl_alignof(slot2))
#define __ffidl_offset3(slot1,slot2,slot3)  \
  ((__ffidl_offset2(slot1,slot2)+__ffidl_sizeof(slot2)+__ffidl_alignof(slot3)-1) & -(long)__ffidl_alignof(slot3))
#define __ffidl_offset4(slot1,slot2,slot3,slot4)  \
  ((__ffidl_offset3(slot1,slot2,slot3)+__ffidl_sizeof(slot3)+__ffidl_alignof(slot4)-1) & -(long)__ffidl_alignof(slot4))
#define __ffidl_alignof(slot) slot->alignment
#define __ffidl_sizeof(slot) slot->size
  if (type->size <= sizeof(__avword))
    type->splittable = 1;
  else if (type->size > 2*sizeof(__avword))
    type->splittable = 0;
  else if (type->nelts == 1)
    type->splittable = ffidl_word_splittable_1(type->elements[0]);
  else if (type->nelts == 2)
    type->splittable = ffidl_word_splittable_2(type->elements[0],type->elements[1]);
  else if (type->nelts == 3)
    type->splittable = ffidl_word_splittable_3(type->elements[0],type->elements[1],type->elements[2]);
  else if (type->nelts == 4)
    type->splittable = ffidl_word_splittable_4(type->elements[0],type->elements[1],type->elements[2],type->elements[3]);
  else
    type->splittable = 0;
#endif
  return JIM_OK;
}
/*
 * cif, ie call signature, management.
 */
/* define a new cif */
static void cif_define(ffidl_client *client, char *cname, ffidl_cif *cif)
{
  entry_define(&client->cifs,cname,(void*)cif);
}
/* lookup an existing cif */
static ffidl_cif *cif_lookup(ffidl_client *client, char *cname)
{
  return entry_lookup(&client->cifs,cname);
}
/* find a cif by it's ffidl_cif */
static Jim_HashEntry *cif_find(ffidl_client *client, ffidl_cif *cif)
{
  return entry_find(&client->cifs,(void *)cif);
}
/* allocate a cif and its parts */
static ffidl_cif *cif_alloc(ffidl_client *client, int argc)
{
  /* allocate storage for:
     the ffidl_cif,
     the argument ffi_type pointers,
     the argument ffidl_types,
     the argument values,
     and the argument value pointers. */
  ffidl_cif *cif;
  cif = (ffidl_cif *)Jim_Alloc(sizeof(ffidl_cif)
			       +argc*sizeof(ffidl_type*) /* atypes */
#if USE_LIBFFI
			       +argc*sizeof(ffi_type*) /* lib_atypes */
#endif /* USE_LIBFFI */
    );
  if (cif == NULL) {
    return NULL;
  }
  /* initialize the cif */
  cif->refs = 0;
  cif->client = client;
  cif->argc = argc;
  cif->atypes = (ffidl_type **)(cif+1);
#if USE_LIBFFI
  cif->lib_atypes = (ffi_type **)(cif->atypes+argc);
#endif /* USE_LIBFFI */
  return cif;
}
/* free a cif */
void cif_free(ffidl_cif *cif)
{
  Jim_Free((void *)cif);
}
/* maintain reference counts on cif's */
static void cif_inc_ref(ffidl_cif *cif)
{
  cif->refs += 1;
}
static void cif_dec_ref(ffidl_cif *cif)
{
  if (--cif->refs == 0) {
    Jim_DeleteHashEntry(&cif->client->cifs, cif_find(cif->client, cif));
    cif_free(cif);
  }
}
/**
 * Parse an argument or return type specification.
 *
 * After invoking this function, @p type points to the @c ffidl_type
 * corresponding to @p typename, and @p argp is initialized
 *
 * @param[in] interp Tcl interpreter.
 * @param[in] client Ffidle data.
 * @param[in] context Context where the type has been found.
 * @param[in] typename Jim_Obj whose string representation is a type name.
 * @param[out] typePtr Points to the place to store the pointer to the parsed @c
 *     ffidl_type.
 * @param[in] valueArea Points to the area where values @p argp values should be
 *     stored.
 * @param[out] valuePtr Points to the place to store the address within @p valueArea
 *     where the arguments are to be retrieved or the return value shall be
 *     placed upon callout.
 * @return JIM_OK if successful, JIM_ERR otherwise.
 */
static int cif_type_parse(Jim_Interp *interp, ffidl_client *client, Jim_Obj *typename, ffidl_type **typePtr)
{
  const char *arg = Jim_GetString(typename, NULL);

  /* lookup the type */
  *typePtr = type_lookup(client, arg);
  if (*typePtr == NULL) {
    AppendResult(interp, "no type defined for: ", arg, NULL);
    return JIM_ERR;
  }
  return JIM_OK;
}

/**
 * Check whether a type may be used in a particular context.
 * @param[in] interp Tcl interpreter.
 * @param[in] context The context the type must be allowed to be used.
 * @param[in] type A ffidl_type.
 * @param[in] typeNameObj The type's name.
 */
static int cif_type_check_context(Jim_Interp *interp, unsigned context,
				  Jim_Obj *typeNameObj, ffidl_type *typePtr)
{
  if ((context & typePtr->class) == 0) {
    const char *typeName = Jim_GetString(typeNameObj, NULL);
    AppendResult(interp, "type ", typeName, " is not permitted in ",
		     (context&FFIDL_ARG) ? "argument" :  "return",
		     " context.", NULL);
    return JIM_ERR;
  }
  return JIM_OK;
}

#if USE_LIBFFI_RAW_API
/**
 * Check whether we can support the raw API on the @p cif.
 */
static int cif_raw_supported(ffidl_cif *cif)
{
  int i;
  int raw_api_supported;

  raw_api_supported = cif->rtype->typecode != FFIDL_STRUCT;

  for (i = 0; i < cif->argc; i++) {
    ffidl_type *atype = cif->atypes[i];
    /* No raw API for structs and long double (fails assertion on
       libffi < 3.3). */
    if (atype->typecode == FFIDL_STRUCT
#if HAVE_LONG_DOUBLE
	|| atypes->typecode == FFIDL_LONGDOUBLE
#endif
      )
      raw_api_supported = 0;
  }
  return raw_api_supported;
}

/**
 * Prepare the arguments and return value areas and pointers.
 */
static int cif_raw_prep_offsets(ffidl_cif *cif, ptrdiff_t *offsets)
{
  int i;
  ptrdiff_t offset = 0;
  size_t bytes = ffi_raw_size(&cif->lib_cif);

  for (i = 0; i < cif->argc; i += 1) {
    offsets[i] = offset;
    offset += cif->atypes[i]->size;
    /* align offset, so total bytes is correct */
    if (offset & (FFI_SIZEOF_ARG-1))
      offset = (offset|(FFI_SIZEOF_ARG-1))+1;
  }
  if (offset != bytes) {
    fprintf(stderr, "ffidl and libffi disagree about bytes of argument! %d != %d\n", offset, bytes);
    return JIM_ERR;
  }
  return JIM_OK;
}
#endif

/* do any library dependent prep for this cif */
static int cif_prep(ffidl_cif *cif)
{
#if USE_LIBFFI
  ffi_type *lib_rtype;
  int i;
  ffi_type **lib_atypes;
  lib_rtype = cif->rtype->lib_type;
  lib_atypes = cif->lib_atypes;
  for (i = 0; i < cif->argc; i += 1) {
    lib_atypes[i] = cif->atypes[i]->lib_type;
  }
  if (ffi_prep_cif(&cif->lib_cif, cif->protocol, cif->argc, lib_rtype, lib_atypes) != FFI_OK) {
    return JIM_ERR;
  }
#endif
  return JIM_OK;
}
/* find the protocol, ie abi, for this cif */
static int cif_protocol(Jim_Interp *interp, Jim_Obj *obj, int *protocolp, const char **protocolnamep)
{
#if USE_LIBFFI
  if (obj != NULL) {
    int len = 0;
    *protocolnamep = Jim_GetString(obj, &len);
    if (len == 0 || strcmp(*protocolnamep, "default") == 0) {
      *protocolp = FFI_DEFAULT_ABI;
      *protocolnamep = NULL;
#  if defined(X86_WIN64)
    } else if (strcmp(*protocolnamep, "win64") == 0) {
      *protocolp = FFI_WIN64;
#  else	 /* X86_WIN64 */
    } else if (strcmp(*protocolnamep, "cdecl") == 0 ||
	       strcmp(*protocolnamep, "sysv") == 0) {
      *protocolp = FFI_SYSV;
    } else if (strcmp(*protocolnamep, "stdcall") == 0) {
      *protocolp = FFI_STDCALL;
    } else if (strcmp(*protocolnamep, "thiscall") == 0) {
      *protocolp = FFI_THISCALL;
    } else if (strcmp(*protocolnamep, "fastcall") == 0) {
      *protocolp = FFI_FASTCALL;
#    if defined(X86_WIN32)
    } else if (strcmp(*protocolnamep, "mscdecl") == 0) {
      *protocolp = FFI_MS_CDECL;
#    elif defined(X86_64)	/* X86_WIN32 */
    } else if (strcmp(*protocolnamep, "unix64") == 0) {
      *protocolp = FFI_UNIX64;
#    endif  /* X86_64 */
#  endif  /* X86_WIN64 */
    } else {
      AppendResult(interp, "unknown protocol \"", *protocolnamep,
		       "\", must be cdecl or stdcall",
		       NULL);
      return JIM_ERR;
    }
  } else {
    *protocolp = FFI_DEFAULT_ABI;
    *protocolnamep = NULL;
  }
#elif USE_LIBFFCALL
  *protocolp = 0;
  *protocolnamep = NULL;
#endif	/* USE_LIBFFCALL */
  return JIM_OK;
}
/*
 * parse a cif argument list, return type, and protocol,
 * and find or create it in the cif table.
 */
static int cif_parse(Jim_Interp *interp, ffidl_client *client, Jim_Obj *args, Jim_Obj *ret, Jim_Obj *pro, ffidl_cif **cifp)
{
  int argc, protocol, i;
  Jim_Obj **argv;
  const char *protocolname;
  Jim_DString signature;
  ffidl_cif *cif = NULL;
  /* fetch argument types */
  if (SetListFromAny(interp, args) != JIM_OK) return JIM_ERR;
  argc = args->internalRep.listValue.len;
  argv = args->internalRep.listValue.ele;
  /* fetch protocol */
  if (cif_protocol(interp, pro, &protocol, &protocolname) == JIM_ERR) return JIM_ERR;
  /* build the cif signature key */
  Jim_DStringInit(&signature);
  if (protocolname != NULL) {
    Jim_DStringAppend(&signature, protocolname, -1);
    Jim_DStringAppend(&signature, " ", 1);
  }
  Jim_DStringAppend(&signature, Jim_GetString(ret, NULL), -1);
  Jim_DStringAppend(&signature, "(", 1);
  for (i = 0; i < argc; i += 1) {
    if (i != 0) Jim_DStringAppend(&signature, ",", 1);
    Jim_DStringAppend(&signature, Jim_GetString(argv[i], NULL), -1);
  }
  Jim_DStringAppend(&signature, ")", 1);
  /* lookup the signature in the cif hash */
  cif = cif_lookup(client, Jim_DStringValue(&signature));
  if (cif == NULL) {
    cif = cif_alloc(client, argc);
    cif->protocol = protocol;
    if (cif == NULL) {
      AppendResult(interp, "couldn't allocate the ffidl_cif", NULL); 
      goto error;
    }
    /* parse return value spec */
    if (cif_type_parse(interp, client, ret, &cif->rtype) == JIM_ERR) {
      goto error;
    }
    /* parse arg specs */
    for (i = 0; i < argc; i += 1)
      if (cif_type_parse(interp, client, argv[i], &cif->atypes[i]) == JIM_ERR) {
	goto error;
      }
    /* see if we done right */
    if (cif_prep(cif) != JIM_OK) {
      AppendResult(interp, "type definition error", NULL);
      goto error;
    }
    /* define the cif */
    cif_define(client, Jim_DStringValue(&signature), cif);
    Jim_SetEmptyResult(interp);
  }
  /* free the signature string */
  Jim_DStringFree(&signature);
  /* mark the cif as referenced */
  cif_inc_ref(cif);
  /* return success */
  *cifp = cif;
  return JIM_OK;
error:
  if (cif) {
    cif_free(cif);
  }
  Jim_DStringFree(&signature);
  return JIM_ERR;
}
/*
 * callout management
 */
/* define a new callout */
static void callout_define(ffidl_client *client, const char *pname, ffidl_callout *callout)
{
  entry_define(&client->callouts,pname,(void*)callout);
}
/* lookup an existing callout */
static ffidl_callout *callout_lookup(ffidl_client *client, const char *pname)
{
  return entry_lookup(&client->callouts,pname);
}
/* find a callout by it's ffidl_callout */
static Jim_HashEntry *callout_find(ffidl_client *client, ffidl_callout *callout)
{
  return entry_find(&client->callouts,(void *)callout);
}
/* cleanup on ffidl_callout_call deletion */
static void callout_delete(Jim_Interp* interp, ClientData clientData)
{
  ffidl_callout *callout = (ffidl_callout *)clientData;
  Jim_HashEntry *entry = callout_find(callout->client, callout);
  if (entry) {
    cif_dec_ref(callout->cif);
    Jim_DeleteHashEntry(&callout->client->callouts, entry);
    /* cb750mark 2019-12-09:  moved this Free to the final line to prevent a possible use-after-free bug. */
    Jim_Free((void *)callout);
  }
}
/**
 * Parse an argument or return type specification.
 *
 * After invoking this function, @p type points to the @c ffidl_type
 * corresponding to @p typename, and @p argp is initialized
 *
 * @param[in] interp Tcl interpreter.
 * @param[in] client Ffidle data.
 * @param[in] context Context where the type has been found.
 * @param[in] typename Jim_Obj whose string representation is a type name.
 * @param[out] typePtr Points to the place to store the pointer to the parsed @c
 *     ffidl_type.
 * @param[in] valueArea Points to the area where values @p argp values should be
 *     stored.
 * @param[out] valuePtr Points to the place to store the address within @p valueArea
 *     where the arguments are to be retrieved or the return value shall be
 *     placed upon callout.
 * @return JIM_OK if successful, JIM_ERR otherwise.
 */
static int callout_prep_value(Jim_Interp *interp, unsigned context,
			      Jim_Obj *typeNameObj, ffidl_type *typePtr,
			      ffidl_value *valueArea, void **valuePtr)
{
  char buff[128];

  /* test the context */
  if (cif_type_check_context(interp, context, typeNameObj, typePtr) != JIM_OK) {
    return JIM_ERR;
  }
  /* set arg value pointer */
  switch (typePtr->typecode) {
  case FFIDL_VOID:
    /* libffi depends on this being NULL on some platforms ! */
    *valuePtr = NULL;
    break;
  case FFIDL_STRUCT:
    /* Will be set to a pointer to the structure's contents. */
    *valuePtr = NULL;
    break;
  case FFIDL_INT:
  case FFIDL_FLOAT:
  case FFIDL_DOUBLE:
#if HAVE_LONG_DOUBLE
  case FFIDL_LONGDOUBLE:
#endif
  case FFIDL_UINT8:
  case FFIDL_SINT8:
  case FFIDL_UINT16:
  case FFIDL_SINT16:
  case FFIDL_UINT32:
  case FFIDL_SINT32:
#if HAVE_INT64
  case FFIDL_UINT64:
  case FFIDL_SINT64:
#endif
  case FFIDL_PTR:
  case FFIDL_PTR_BYTE:
  case FFIDL_PTR_OBJ:
  case FFIDL_PTR_UTF8:
  case FFIDL_PTR_UTF16:
  case FFIDL_PTR_VAR:
  case FFIDL_PTR_PROC:
    *valuePtr = (void *)valueArea;
    break;
  default:
    sprintf(buff, "unknown ffidl_type.t = %d", typePtr->typecode);
    AppendResult(interp, buff, NULL);
    return JIM_ERR;
  }
  return JIM_OK;
}

static int callout_prep(ffidl_callout *callout)
{
#if USE_LIBFFI_RAW_API
  int i;
  ffidl_cif *cif = callout->cif;

  callout->use_raw_api = cif_raw_supported(cif);
  callout->use_raw_api = 0;

  if (callout->use_raw_api) {
    /* rewrite callout->args[i] into a stack image */
    ptrdiff_t *offsets;
    offsets = (ptrdiff_t *)Jim_Alloc(sizeof(ptrdiff_t) * cif->argc);
    if (JIM_OK != cif_raw_prep_offsets(cif, offsets)) {
      Jim_Free((void *)offsets);
      return JIM_ERR;
    }
    /* fprintf(stderr, "using raw api for %d args\n", cif->argc); */
    for (i = 0; i < cif->argc; i++) {
      /* set args[i] to args[0]+offset */
      /* fprintf(stderr, "  arg[%d] was %08x ...", i, callout->args[i]); */
      callout->args[i] = (void *)(((char *)callout->args[0])+offsets[i]);
      /* fprintf(stderr, " becomes %08x\n", callout->args[i]); */
    }
    /* fprintf(stderr, "  final offset %d, bytes %d\n", offset, bytes); */
    Jim_Free((void *)offsets);
  }
#endif
  return JIM_OK;
}

/* make a call */
/* consider what happens if we reenter using the same cif */  
static void callout_call(ffidl_callout *callout)
{
  ffidl_cif *cif = callout->cif;
#if USE_LIBFFI
#if USE_LIBFFI_RAW_API
  if (callout->use_raw_api)
    ffi_raw_call(&cif->lib_cif, callout->fn, callout->ret, (ffi_raw *)callout->args[0]);
  else
    ffi_call(&cif->lib_cif, callout->fn, callout->ret, callout->args);
#else
  ffi_call(&cif->lib_cif, callout->fn, callout->ret, callout->args);
#endif
#elif USE_LIBFFCALL
  av_alist alist;
  int i;
  switch (cif->rtype->typecode) {
  case FFIDL_VOID:
    av_start_void(alist,callout->fn);
    break;
  case FFIDL_INT:
    av_start_int(alist,callout->fn,callout->ret);
    break;
  case FFIDL_FLOAT:
    av_start_float(alist,callout->fn,callout->ret);
    break;
  case FFIDL_DOUBLE:
    av_start_double(alist,callout->fn,callout->ret);
    break;
  case FFIDL_UINT8:
    av_start_uint8(alist,callout->fn,callout->ret);
    break;
  case FFIDL_SINT8:
    av_start_sint8(alist,callout->fn,callout->ret);
    break;
  case FFIDL_UINT16:
    av_start_uint16(alist,callout->fn,callout->ret);
    break;
  case FFIDL_SINT16:
    av_start_sint16(alist,callout->fn,callout->ret);
    break;
  case FFIDL_UINT32:
    av_start_uint32(alist,callout->fn,callout->ret);
    break;
  case FFIDL_SINT32:
    av_start_sint32(alist,callout->fn,callout->ret);
    break;
#if HAVE_INT64
  case FFIDL_UINT64:
    av_start_uint64(alist,callout->fn,callout->ret);
    break;
  case FFIDL_SINT64:
    av_start_sint64(alist,callout->fn,callout->ret);
    break;
#endif
  case FFIDL_STRUCT:
    _av_start_struct(alist,callout->fn,cif->rtype->size,cif->rtype->splittable,callout->ret);
    break;
  case FFIDL_PTR:
  case FFIDL_PTR_OBJ:
  case FFIDL_PTR_UTF8:
  case FFIDL_PTR_UTF16:
  case FFIDL_PTR_BYTE:
  case FFIDL_PTR_VAR:
#if USE_CALLBACKS
  case FFIDL_PTR_PROC:
#endif
    av_start_ptr(alist,callout->fn,void *,callout->ret);
    break;
  }

  for (i = 0; i < cif->argc; i += 1) {
    switch (cif->atypes[i]->typecode) {
    case FFIDL_INT:
      av_int(alist,*(int *)callout->args[i]);
      continue;
    case FFIDL_FLOAT:
      av_float(alist,*(float *)callout->args[i]);
      continue;
    case FFIDL_DOUBLE:
      av_double(alist,*(double *)callout->args[i]);
      continue;
    case FFIDL_UINT8:
      av_uint8(alist,*(UINT8_T *)callout->args[i]);
      continue;
    case FFIDL_SINT8:
      av_sint8(alist,*(SINT8_T *)callout->args[i]);
      continue;
    case FFIDL_UINT16:
      av_uint16(alist,*(UINT16_T *)callout->args[i]);
      continue;
    case FFIDL_SINT16:
      av_sint16(alist,*(SINT16_T *)callout->args[i]);
      continue;
    case FFIDL_UINT32:
      av_uint32(alist,*(UINT32_T *)callout->args[i]);
      continue;
    case FFIDL_SINT32:
      av_sint32(alist,*(SINT32_T *)callout->args[i]);
      continue;
#if HAVE_INT64
    case FFIDL_UINT64:
      av_uint64(alist,*(UINT64_T *)callout->args[i]);
      continue;
    case FFIDL_SINT64:
      av_sint64(alist,*(SINT64_T *)callout->args[i]);
      continue;
#endif
    case FFIDL_STRUCT:
      _av_struct(alist,cif->atypes[i]->size,cif->atypes[i]->alignment,callout->args[i]);
      continue;
    case FFIDL_PTR:
    case FFIDL_PTR_OBJ:
    case FFIDL_PTR_UTF8:
    case FFIDL_PTR_UTF16:
    case FFIDL_PTR_BYTE:
    case FFIDL_PTR_VAR:
#if USE_CALLBACKS
    case FFIDL_PTR_PROC:
#endif
      av_ptr(alist,void *,*(void **)callout->args[i]);
      continue;
    }
    /* Note: change "continue" to "break" if further work must be done here. */
  }
  av_call(alist);
#endif
}
/*
 * lib management, but note we never free a lib
 * because we cannot know how often it is used.
 */
/* define a new lib */
static void lib_define(ffidl_client *client, const char *lname, void *handle, void* unload)
{
  ffidl_lib *libentry = (ffidl_lib *)Jim_Alloc(sizeof(ffidl_lib));
  libentry->loadHandle = handle;
  libentry->unloadProc = unload;
  entry_define(&client->libs,lname,libentry);
}
/* lookup an existing type */
static ffidl_LoadHandle lib_lookup(ffidl_client *client,
				   const char *lname,
				   ffidl_UnloadProc *unload)
{
  ffidl_lib *libentry = entry_lookup(&client->libs,lname);
  if (libentry) {
    if (unload) {
      *unload = libentry->unloadProc;
    }
    return libentry->loadHandle;
  } else {
    return NULL;
  }
}
#if USE_CALLBACKS
/*
 * callback management
 */
/* free a defined callback */
static void callback_free(ffidl_callback *callback)
{
  if (callback) {
    int i;
    cif_dec_ref(callback->cif);
    for (i = 0; i < callback->cmdc; i++) {
      Jim_DecrRefCount(callback->interp, callback->cmdv[i]);
    }
#if USE_LIBFFI
    ffi_closure_free(callback->closure.lib_closure);
#elif USE_LIBFFCALL
    free_callback(callback->closure.lib_closure);
#endif
    Jim_Free((void *)callback);
  }
}
/* define a new callback */
static void callback_define(ffidl_client *client, const char *cname, ffidl_callback *callback)
{
  ffidl_callback *old_callback = NULL;
  /* if callback is already defined, clean it up. */
  old_callback = entry_lookup(&client->callbacks,cname);
  callback_free(old_callback);
  entry_define(&client->callbacks,cname,(void*)callback);
}
/* lookup an existing callback */
static ffidl_callback *callback_lookup(ffidl_client *client, const char *cname)
{
  return entry_lookup(&client->callbacks,cname);
}
/* find a callback by it's ffidl_callback */
/*
static Jim_HashEntry *callback_find(ffidl_client *client, ffidl_callback *callback)
{
  return entry_find(&client->callbacks,(void *)callback);
}
*/
/* delete a callback definition */
/*
static void callback_delete(ffidl_client *client, ffidl_callback *callback)
{
  Jim_HashEntry *entry = callback_find(client, callback);
  if (entry) {
    callback_free(callback);
    Jim_DeleteHashEntry(entry);
  }
}
*/

static void BackgroundError(Jim_Interp *interp) {
    /* implementation borrowed from Jim_EvalObjBackground().  in future it should be factored out from there
    so it doesn't have to be duplicated here.  revisit.  */
    
    Jim_EventLoop *eventLoop;
    eventLoop = Jim_GetAssocData(interp, "eventloop");

    /* Try to report the error via the bgerror proc */
    Jim_Obj *objv[2];
    int rc = JIM_ERR;

    objv[0] = Jim_NewStringObj(interp, "bgerror", -1);
    objv[1] = Jim_GetResult(interp);
    Jim_IncrRefCount(objv[0]);
    Jim_IncrRefCount(objv[1]);
    if (Jim_GetCommand(interp, objv[0], JIM_NONE) == NULL || (rc = Jim_EvalObjVector(interp, 2, objv)) != JIM_OK) {
        if (rc == JIM_BREAK) {
            /* No more bgerror calls */
            eventLoop->suppress_bgerror++;
        }
        else {
            /* Report the error to stderr. */
            Jim_MakeErrorMessage(interp);
            fprintf(stderr, "%s\n", Jim_String(Jim_GetResult(interp)));
            /* And reset the result */
            Jim_SetResultString(interp, "", -1);
        }
    }
    Jim_DecrRefCount(interp, objv[0]);
    Jim_DecrRefCount(interp, objv[1]);
}

#if USE_LIBFFI
/* call a tcl proc from a libffi closure */
static void callback_callback(ffi_cif *fficif, void *ret, void **args, void *user_data)
{
  ffidl_callback *callback = (ffidl_callback *)user_data;
  Jim_Interp *interp = callback->interp;
  ffidl_cif *cif = callback->cif;
  Jim_Obj **objv, *obj;
  char buff[128];
  int i, status;
  long ltmp;
  double dtmp;
#if HAVE_INT64
  Ffidl_Int64 wtmp;
#endif
#ifdef JIM_DEBUG_PANIC
  /* test for valid scope */
  if (interp == NULL) {
    JimPanicDump(1, "callback called out of scope!\n");
  }
#endif
  /* initialize argument list */
  objv = callback->cmdv+callback->cmdc;
  /* fetch and convert argument values */
  for (i = 0; i < cif->argc; i += 1) {
    void *argp;
#if USE_LIBFFI_RAW_API
    if (callback->use_raw_api) {
      ptrdiff_t offset = callback->offsets[i] - callback->offsets[0];
      argp = (void *)(((char *)args)+offset);
    } else {
      argp = args[i];
    }
#else
    argp = args[i];
#endif
    switch (cif->atypes[i]->typecode) {
    case FFIDL_INT:
      objv[i] = Jim_NewLongObj(interp, (long)(*(int *)argp));
      break;
    case FFIDL_FLOAT:
      objv[i] = Jim_NewDoubleObj(interp, (double)(*(float *)argp));
      break;
    case FFIDL_DOUBLE:
      objv[i] = Jim_NewDoubleObj(interp, *(double *)argp);
      break;
#if HAVE_LONG_DOUBLE
    case FFIDL_LONGDOUBLE:
      objv[i] = Jim_NewDoubleObj(interp, (double)(*(long double *)argp));
      break;
#endif
    case FFIDL_UINT8:
      objv[i] = Jim_NewLongObj(interp, (long)(*(UINT8_T *)argp));
      break;
    case FFIDL_SINT8:
      objv[i] = Jim_NewLongObj(interp, (long)(*(SINT8_T *)argp));
      break;
    case FFIDL_UINT16:
      objv[i] = Jim_NewLongObj(interp, (long)(*(UINT16_T *)argp));
      break;
    case FFIDL_SINT16:
      objv[i] = Jim_NewLongObj(interp, (long)(*(SINT16_T *)argp));
      break;
    case FFIDL_UINT32:
      objv[i] = Jim_NewLongObj(interp, (long)(*(UINT32_T *)argp));
      break;
    case FFIDL_SINT32:
      objv[i] = Jim_NewLongObj(interp, (long)(*(SINT32_T *)argp));
      break;
#if HAVE_INT64
    case FFIDL_UINT64:
      objv[i] = Ffidl_NewInt64Obj(interp, (Ffidl_Int64)(*(UINT64_T *)argp));
      break;
    case FFIDL_SINT64:
      objv[i] = Ffidl_NewInt64Obj(interp, (Ffidl_Int64)(*(SINT64_T *)argp));
      break;
#endif
    case FFIDL_STRUCT:
      /* risky.  revisit. */
      objv[i] = Jim_NewStringObj(interp, (char *)argp, cif->atypes[i]->size);
      break;
    case FFIDL_PTR:
      objv[i] = Ffidl_NewPointerObj(interp, (*(void **)argp));
      break;
    case FFIDL_PTR_OBJ:
      objv[i] = *(Jim_Obj **)argp;
      break;
    case FFIDL_PTR_UTF8:
      objv[i] = Jim_NewStringObjUtf8(interp, *(char **)argp, -1);
      break;
/* not supported yet in Jim port. 
    case FFIDL_PTR_UTF16:
      objv[i] = Jim_NewUnicodeObj(interp, *(Jim_UniChar **)argp, -1);
      break; */
    default:
      sprintf(buff, "unimplemented type for callback argument: %d", cif->atypes[i]->typecode);
      AppendResult(interp, buff, NULL);
      while (i-- >= 0) {
	Jim_DecrRefCount(interp, objv[i]);
      }
      goto escape;
    }
    Jim_IncrRefCount(objv[i]);
  }
  /* call */
  /* eliminated TCL_EVAL_GLOBAL for Jim port.  risky.  revisit.  */
  status = Jim_EvalObjVector(interp, callback->cmdc+cif->argc, callback->cmdv);
  /* clean up arguments */
  for (i = 0; i < cif->argc; i++) {
    Jim_DecrRefCount(interp, objv[i]);
  }
  if (status == JIM_ERR) {
    goto escape;
  }
  /* fetch return value */
  obj = Jim_GetResult(interp);
  if (cif->rtype->class & FFIDL_GETINT) {
    if (obj->typePtr == ffidl_double_ObjType) {
      if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR) {
	AppendResult(interp, ", converting callback return value", NULL);
	goto escape;
      }
      ltmp = (long)dtmp;
      if (dtmp != ltmp) {
	if (Jim_GetLong(interp, obj, &ltmp) == JIM_ERR) {
	  AppendResult(interp, ", converting callback return value", NULL);
	  goto escape;
	}
      }
    } else if (Jim_GetLong(interp, obj, &ltmp) == JIM_ERR) {
      AppendResult(interp, ", converting callback return value", NULL);
      goto escape;
    }
#if HAVE_INT64
  } else if (cif->rtype->class & FFIDL_GETWIDEINT) {
    if (obj->typePtr == ffidl_double_ObjType) {
      if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR) {
	AppendResult(interp, ", converting callback return value", NULL);
	goto escape;
      }
      wtmp = (Ffidl_Int64)dtmp;
      if (dtmp != wtmp) {
	if (Ffidl_GetInt64FromObj(interp, obj, &wtmp) == JIM_ERR) {
	  AppendResult(interp, ", converting callback return value", NULL);
	  goto escape;
	}
      }
    } else if (Ffidl_GetInt64FromObj(interp, obj, &wtmp) == JIM_ERR) {
      AppendResult(interp, ", converting callback return value", NULL);
      goto escape;
    }
#endif
  } else if (cif->rtype->class & FFIDL_GETDOUBLE) {
    if (obj->typePtr == ffidl_int_ObjType) {
      if (Jim_GetLong(interp, obj, &ltmp) == JIM_ERR) {
	AppendResult(interp, ", converting callback return value", NULL);
	goto escape;
      }
      dtmp = (double)ltmp;
      if (dtmp != ltmp) {
	if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR) {
	  AppendResult(interp, ", converting callback return value", NULL);
	  goto escape;
	}
      }
#if HAVE_WIDE_INT
    } else if (obj->typePtr == ffidl_wideInt_ObjType) {
      if (Jim_GetWideIntFromObj(interp, obj, &wtmp) == JIM_ERR) {
	AppendResult(interp, ", converting callback return value", NULL);
	goto escape;
      }
      dtmp = (double)wtmp;
      if (dtmp != wtmp) {
	if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR) {
	  AppendResult(interp, ", converting callback return value", NULL);
	  goto escape;
	}
      }
#endif
    } else if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR) {
      AppendResult(interp, ", converting callback return value", NULL);
      goto escape;
    }
  }
  
  /* convert return value */
  switch (cif->rtype->typecode) {
  case FFIDL_VOID:	break;
  case FFIDL_INT:	FFIDL_RVALUE_POKE_WIDENED(INT, ret, ltmp); break;
  case FFIDL_FLOAT:	FFIDL_RVALUE_POKE_WIDENED(FLOAT, ret, dtmp); break;
  case FFIDL_DOUBLE:	FFIDL_RVALUE_POKE_WIDENED(DOUBLE, ret, dtmp); break;
#if HAVE_LONG_DOUBLE
  case FFIDL_UINT8:	FFIDL_RVALUE_POKE_WIDENED(LONGDOUBLE, ret, ltmp); break;
#endif
  case FFIDL_UINT8:	FFIDL_RVALUE_POKE_WIDENED(UINT8, ret, ltmp); break;
  case FFIDL_SINT8:	FFIDL_RVALUE_POKE_WIDENED(SINT8, ret, ltmp); break;
  case FFIDL_UINT16:	FFIDL_RVALUE_POKE_WIDENED(UINT16, ret, ltmp); break;
  case FFIDL_SINT16:	FFIDL_RVALUE_POKE_WIDENED(SINT16, ret, ltmp); break;
  case FFIDL_UINT32:	FFIDL_RVALUE_POKE_WIDENED(UINT32, ret, ltmp); break;
  case FFIDL_SINT32:	FFIDL_RVALUE_POKE_WIDENED(SINT32, ret, ltmp); break;
#if HAVE_INT64
  case FFIDL_UINT64:	FFIDL_RVALUE_POKE_WIDENED(UINT64, ret, wtmp); break;
  case FFIDL_SINT64:	FFIDL_RVALUE_POKE_WIDENED(SINT64, ret, wtmp); break;
#endif
  case FFIDL_STRUCT:
    {
      if (obj->length != cif->rtype->size) {
	Jim_SetEmptyResult(interp);
	sprintf(buff, "byte array for callback struct return has %u bytes instead of %lu", 
        obj->length, (long)(cif->rtype->size));
	AppendResult(interp, buff, NULL);
	goto escape;
      }
      memcpy(ret, obj->bytes, cif->rtype->size);
      break;
    }
#if FFIDL_POINTER_IS_LONG
  case FFIDL_PTR:	FFIDL_RVALUE_POKE_WIDENED(PTR, ret, ltmp); break;
#else
  case FFIDL_PTR:	FFIDL_RVALUE_POKE_WIDENED(PTR, ret, wtmp); break;
#endif
  case FFIDL_PTR_OBJ:	FFIDL_RVALUE_POKE_WIDENED(PTR, ret, obj); break;
  default:
    Jim_SetEmptyResult(interp);
    sprintf(buff, "unimplemented type for callback return: %d", cif->rtype->typecode);
    AppendResult(interp, buff, NULL);
    goto escape;
  }
  /* done */
  return;
escape:
  BackgroundError(interp);
  memset(ret, 0, cif->rtype->size);
}
#elif USE_LIBFFCALL
static void callback_callback(void *user_data, va_alist alist)
{
  ffidl_callback *callback = (ffidl_callback *)user_data;
  Jim_Interp *interp = callback->interp;
  ffidl_cif *cif = callback->cif;
  Jim_Obj **objv, *obj;
  char buff[128];
  int i, status;
  long ltmp;
  double dtmp;
#if HAVE_INT64
  Ffidl_Int64 wtmp;
#endif
#ifdef JIM_DEBUG_PANIC
  /* test for valid scope */
  if (interp == NULL) {
    JimPanicDump(1, "callback called out of scope!\n");
  }
#endif
  /* initialize argument list */
  objv = callback->cmdv+callback->cmdc;
  /* start */
  switch (cif->rtype->typecode) {
  case FFIDL_VOID:	va_start_void(alist); break;
  case FFIDL_INT:	va_start_int(alist); break;
  case FFIDL_FLOAT:	va_start_float(alist); break;
  case FFIDL_DOUBLE:	va_start_double(alist); break;
  case FFIDL_UINT8:	va_start_uint8(alist); break;
  case FFIDL_SINT8:	va_start_sint8(alist); break;
  case FFIDL_UINT16:	va_start_uint16(alist); break;
  case FFIDL_SINT16:	va_start_sint16(alist); break;
  case FFIDL_UINT32:	va_start_uint32(alist); break;
  case FFIDL_SINT32:	va_start_sint32(alist); break;
#if HAVE_INT64
  case FFIDL_UINT64:	va_start_uint64(alist); break;
  case FFIDL_SINT64:	va_start_sint64(alist); break;
#endif
  case FFIDL_STRUCT:	_va_start_struct(alist,cif->rtype->size,cif->rtype->alignment,cif->rtype->splittable); break;
  case FFIDL_PTR:	va_start_ptr(alist,void *); break;
  case FFIDL_PTR_OBJ:	va_start_ptr(alist,Jim_Obj *); break;
  default:
    Jim_ResetResult(interp);
    sprintf(buff, "unimplemented type for callback return: %d", cif->rtype->typecode);
    AppendResult(interp, buff, NULL);
    goto escape;
  }
  /* fetch and convert argument values */
  for (i = 0; i < cif->argc; i++) {
    switch (cif->atypes[i]->typecode) {
    case FFIDL_INT:
      objv[i] = Jim_NewLongObj((long)va_arg_int(alist));
      break;
    case FFIDL_FLOAT:
      objv[i] = Jim_NewDoubleObj((double)va_arg_float(alist));
      break;
    case FFIDL_DOUBLE:
      objv[i] = Jim_NewDoubleObj(va_arg_double(alist));
      break;
    case FFIDL_UINT8:
      objv[i] = Jim_NewLongObj((long)va_arg_uint8(alist));
      break;
    case FFIDL_SINT8:
      objv[i] = Jim_NewLongObj((long)va_arg_sint8(alist));
      break;
    case FFIDL_UINT16:
      objv[i] = Jim_NewLongObj((long)va_arg_uint16(alist));
      break;
    case FFIDL_SINT16:
      objv[i] = Jim_NewLongObj((long)va_arg_sint16(alist));
      break;
    case FFIDL_UINT32:
      objv[i] = Jim_NewLongObj((long)va_arg_uint32(alist));
      break;
    case FFIDL_SINT32:
      objv[i] = Jim_NewLongObj((long)va_arg_sint32(alist));
      break;
#if HAVE_INT64
    case FFIDL_UINT64:
      objv[i] = Ffidl_NewInt64Obj((long)va_arg_uint64(alist));
      break;
    case FFIDL_SINT64:
      objv[i] = Ffidl_NewInt64Obj((long)va_arg_sint64(alist));
      break;
#endif
    case FFIDL_STRUCT:
      objv[i] = Jim_NewByteArrayObj(_va_arg_struct(alist,
						   cif->atypes[i]->size,
						   cif->atypes[i]->alignment),
				    cif->atypes[i]->size);
      break;
    case FFIDL_PTR:
      objv[i] = Ffidl_NewPointerObj(interp, va_arg_ptr(alist,void *));
      break;
    case FFIDL_PTR_OBJ:
      objv[i] = va_arg_ptr(alist,Jim_Obj *);
      break;
    case FFIDL_PTR_UTF8:
      objv[i] = Jim_NewStringObj(va_arg_ptr(alist,char *), -1);
      break;
    case FFIDL_PTR_UTF16:
      objv[i] = Jim_NewUnicodeObj(va_arg_ptr(alist,Jim_UniChar *), -1);
      break;
    default:
      sprintf(buff, "unimplemented type for callback argument: %d", cif->atypes[i]->typecode);
      AppendResult(interp, buff, NULL);
      while (i-- >= 0) {
	Jim_DecrRefCount(interp, objv[i]);
      }
      goto escape;
    }
    Jim_IncrRefCount(objv[i]);
  }
  /* call */
  status = Jim_EvalObjv(interp, callback->cmdc+cif->argc, callback->cmdv, TCL_EVAL_GLOBAL);
  /* clean up arguments */
  for (i = 0; i < cif->argc; i++) {
    Jim_DecrRefCount(interp, objv[i]);
  }
  if (status == JIM_ERR) {
    goto escape;
  }
  /* fetch return value */
  obj = Jim_GetObjResult(interp);
  if (cif->rtype->class & FFIDL_GETINT) {
    if (obj->typePtr == ffidl_double_ObjType) {
      if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR) {
	AppendResult(interp, ", converting callback return value", NULL);
	goto escape;
      }
      ltmp = (long)dtmp;
      if (dtmp != ltmp)
	if (Jim_GetLong(interp, obj, &ltmp) == JIM_ERR) {
	  AppendResult(interp, ", converting callback return value", NULL);
	  goto escape;
	}
    } else if (Jim_GetLong(interp, obj, &ltmp) == JIM_ERR) {
      AppendResult(interp, ", converting callback return value", NULL);
      goto escape;
    }
#if HAVE_INT64
  } else if (cif->rtype->class & FFIDL_GETWIDEINT) {
    if (obj->typePtr == ffidl_double_ObjType) {
      if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR) {
	AppendResult(interp, ", converting callback return value", NULL);
	goto escape;
      }
      wtmp = (Ffidl_Int64)dtmp;
      if (dtmp != wtmp) {
	if (Ffidl_GetInt64FromObj(interp, obj, &wtmp) == JIM_ERR) {
	  AppendResult(interp, ", converting callback return value", NULL);
	  goto escape;
	}
      }
    } else if (Ffidl_GetInt64FromObj(interp, obj, &wtmp) == JIM_ERR) {
      AppendResult(interp, ", converting callback return value", NULL);
      goto escape;
    }
#endif
  } else if (cif->rtype->class & FFIDL_GETDOUBLE) {
    if (obj->typePtr == ffidl_int_ObjType) {
      if (Jim_GetLong(interp, obj, &ltmp) == JIM_ERR) {
	AppendResult(interp, ", converting callback return value", NULL);
	goto escape;
      }
      dtmp = (double)ltmp;
      if (dtmp != ltmp) {
	if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR) {
	  AppendResult(interp, ", converting callback return value", NULL);
	  goto escape;
	}
      }
#if HAVE_WIDE_INT
    } else if (obj->typePtr == ffidl_wideInt_ObjType) {
      if (Jim_GetWideIntFromObj(interp, obj, &wtmp) == JIM_ERR) {
	AppendResult(interp, ", converting callback return value", NULL);
	goto escape;
      }
      dtmp = (double)wtmp;
      if (dtmp != wtmp) {
	if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR) {
	  AppendResult(interp, ", converting callback return value", NULL);
	  goto escape;
	}
      }
#endif
    } else if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR) {
      AppendResult(interp, ", converting callback return value", NULL);
      goto escape;
    }
  }
  
  /* convert return value */
  switch (cif->rtype->typecode) {
  case FFIDL_VOID:	va_return_void(alist); break;
  case FFIDL_INT:	va_return_int(alist, ltmp); break;
  case FFIDL_FLOAT:	va_return_float(alist, dtmp); break;
  case FFIDL_DOUBLE:	va_return_double(alist, dtmp); break;
  case FFIDL_UINT8:	va_return_uint8(alist, ltmp); break;
  case FFIDL_SINT8:	va_return_sint8(alist, ltmp); break;
  case FFIDL_UINT16:	va_return_uint16(alist, ltmp); break;
  case FFIDL_SINT16:	va_return_sint16(alist, ltmp); break;
  case FFIDL_UINT32:	va_return_uint32(alist, ltmp); break;
  case FFIDL_SINT32:	va_return_sint32(alist, ltmp); break;
#if HAVE_INT64
  case FFIDL_UINT64:	va_return_uint64(alist, wtmp); break;
  case FFIDL_SINT64:	va_return_sint64(alist, wtmp); break;
#endif
  case FFIDL_STRUCT:	
    {
      int len;
      void *bytes = Jim_GetByteArrayFromObj(obj, &len);
      if (len != cif->rtype->size) {
	Jim_ResetResult(interp);
	sprintf(buff, "byte array for callback struct return has %u bytes instead of %lu", len, (long)(cif->rtype->size));
	AppendResult(interp, buff, NULL);
	goto escape;
      }
      _va_return_struct(alist, cif->rtype->size, cif->rtype->alignment, bytes);
      break;
    }
  case FFIDL_PTR:	va_return_ptr(alist, void *, ltmp); break;
  case FFIDL_PTR_OBJ:	va_return_ptr(alist, Jim_Obj *, obj); break;
  default:
    Jim_ResetResult(interp);
    sprintf(buff, "unimplemented type for callback return: %d", cif->rtype->typecode);
    AppendResult(interp, buff, NULL);
    goto escape;
  }
  /* done */
  return;
escape:
  BackgroundError(interp);
}
#endif
#endif

/*
 * Client management.
 */

static const Jim_HashTableType StringToPointerHashTableType = {
    JimStringCopyHTHashFunction,    /* hash function */
    JimStringCopyHTDup,             /* key dup */
    NULL,                           /* val dup */
    JimStringCopyHTKeyCompare,      /* key compare */
    JimStringCopyHTKeyDestructor,   /* key destructor */
    NULL                            /* val destructor */
};
 
/* client interp deletion callback for cleanup */
static void client_delete(Jim_Interp *interp)
{
  ffidl_client *client = (ffidl_client *)Jim_CmdPrivData(interp);

  Jim_HashEntry *entry;
  Jim_HashTableIterator * i;

  /* there should be no callouts left */
  i = Jim_GetHashTableIterator(&client->callouts);
  while ((entry = Jim_NextHashEntry(i))) {
    const char *name = Jim_GetHashEntryKey(entry);
    /* Couldn't do this while traversing the hash table anyway */
    /* Jim_DeleteCommand(interp, name); */
    fprintf(stderr, "error - dangling callout in client_delete: %s\n", name);
  }
  Jim_Free(i);

#if USE_CALLBACKS
  /* free all callbacks */
  i = Jim_GetHashTableIterator(&client->callbacks);
  while ((entry = Jim_NextHashEntry(i))) {
    ffidl_callback *callback = Jim_GetHashEntryVal(entry);
    callback_free(callback);
  }
  Jim_Free(i);
#endif

  /* there should be no cifs left */
  i = Jim_GetHashTableIterator(&client->cifs);
  while ((entry = Jim_NextHashEntry(i))) {
    char *signature = Jim_GetHashEntryKey(entry);
    fprintf(stderr, "error - dangling ffidl_cif in client_delete: %s\n",signature);
  }
  Jim_Free(i);

  /* free all allocated typedefs */
  i = Jim_GetHashTableIterator(&client->types);
  while ((entry = Jim_NextHashEntry(i))) {
    ffidl_type *type = Jim_GetHashEntryVal(entry);
    if ((type->class & FFIDL_STATIC_TYPE) == 0) {
      type_dec_ref(type);
    }
  }
  Jim_Free(i);

  /* free all libs */
  i = Jim_GetHashTableIterator(&client->libs);
  while ((entry = Jim_NextHashEntry(i))) {
    char *libraryName = Jim_GetHashEntryKey(entry);
    ffidl_lib *libentry = Jim_GetHashEntryVal(entry);
    ffidlclose(interp, libraryName, libentry->loadHandle, libentry->unloadProc);
    Jim_Free((void *)libentry);
  }
  Jim_Free(i);

  /* free hashtables */
  Jim_FreeHashTable(&client->callouts);
#if USE_CALLBACKS
  Jim_FreeHashTable(&client->callbacks);
#endif
  Jim_FreeHashTable(&client->cifs);
  Jim_FreeHashTable(&client->types);
  Jim_FreeHashTable(&client->libs);

  /* free client structure */
  Jim_Free((void *)client);
}
/* client allocation and initialization */
static ffidl_client *client_alloc(Jim_Interp *interp)
{
  ffidl_client *client;

  /* allocate client data structure */
  client = (ffidl_client *)Jim_Alloc(sizeof(ffidl_client));

  /* allocate hashtables for this load */
  Jim_InitHashTable(&client->types,     &StringToPointerHashTableType, NULL);
  Jim_InitHashTable(&client->callouts,  &StringToPointerHashTableType, NULL);
  Jim_InitHashTable(&client->cifs,      &StringToPointerHashTableType, NULL);
  Jim_InitHashTable(&client->libs,      &StringToPointerHashTableType, NULL);
#if USE_CALLBACKS
  Jim_InitHashTable(&client->callbacks, &StringToPointerHashTableType, NULL);
#endif

  /* initialize types */
  type_define(client, "void", &ffidl_type_void);
  type_define(client, "char", &ffidl_type_char);
  type_define(client, "signed char", &ffidl_type_schar);
  type_define(client, "unsigned char", &ffidl_type_uchar);
  type_define(client, "short", &ffidl_type_sshort);
  type_define(client, "unsigned short", &ffidl_type_ushort);
  type_define(client, "int", &ffidl_type_sint);
  type_define(client, "unsigned", &ffidl_type_uint);
  type_define(client, "long", &ffidl_type_slong);
  type_define(client, "unsigned long", &ffidl_type_ulong);
#if HAVE_LONG_LONG
  type_define(client, "long long", &ffidl_type_slonglong);
  type_define(client, "unsigned long long", &ffidl_type_ulonglong);
#endif
  type_define(client, "float", &ffidl_type_float);
  type_define(client, "double", &ffidl_type_double);
#if HAVE_LONG_DOUBLE
  type_define(client, "long double", &ffidl_type_longdouble);
#endif
  type_define(client, "sint8", &ffidl_type_sint8);
  type_define(client, "uint8", &ffidl_type_uint8);
  type_define(client, "sint16", &ffidl_type_sint16);
  type_define(client, "uint16", &ffidl_type_uint16);
  type_define(client, "sint32", &ffidl_type_sint32);
  type_define(client, "uint32", &ffidl_type_uint32);
#if HAVE_INT64
  type_define(client, "sint64", &ffidl_type_sint64);
  type_define(client, "uint64", &ffidl_type_uint64);
#endif
  type_define(client, "pointer", &ffidl_type_pointer);
  type_define(client, "pointer-obj", &ffidl_type_pointer_obj);
  type_define(client, "pointer-utf8", &ffidl_type_pointer_utf8);
/*  type_define(client, "pointer-utf16", &ffidl_type_pointer_utf16); */
  type_define(client, "pointer-byte", &ffidl_type_pointer_byte);
  type_define(client, "pointer-var", &ffidl_type_pointer_var);
#if USE_CALLBACKS
  type_define(client, "pointer-proc", &ffidl_type_pointer_proc);
#endif

  /* arrange for cleanup on interpreter deletion */
/*  Jim_CallWhenDeleted(interp, client_delete, (ClientData)client);
    revisit.  need code to prevent memory leak.  
    looks like the only way Jim supports is by setting a "defer" script on the outermost call frame?  */

  /* finis */
  return client;
}
/*****************************************
 *
 * Functions exported as tcl commands.
 */

/* usage: ::ffidl::info option ?...? */
static int tcl_ffidl_info(Jim_Interp *interp, int objc, Jim_Obj *CONST objv[])
{
  enum {
    command_ix,
    option_ix,
    minargs
  };

  int i;
  const char *arg;
  Jim_HashTable *table;
  Jim_HashEntry *entry;
  Jim_HashTableIterator *iter;
  ffidl_type *type;
  ffidl_client *client = (ffidl_client *)Jim_CmdPrivData(interp);
  static const char *options[] = {
#define INFO_ALIGNOF 0
    "alignof",
#define INFO_CALLBACKS 1
    "callbacks",
#define INFO_CALLOUTS 2
    "callouts",
#define INFO_CANONICAL_HOST 3
    "canonical-host",
#define INFO_FORMAT 4
    "format",
#define INFO_HAVE_INT64 5
    "have-int64",
#define INFO_HAVE_LONG_DOUBLE 6
    "have-long-double",
#define INFO_HAVE_LONG_LONG 7
    "have-long-long",
#define INFO_INTERP 8
    "interp",
#define INFO_LIBRARIES 9
    "libraries",
#define INFO_SIGNATURES 10
    "signatures",
#define INFO_SIZEOF 11
    "sizeof",
#define INFO_TYPEDEFS 12
    "typedefs",
#define INFO_USE_CALLBACKS 13
    "use-callbacks",
#define INFO_USE_FFCALL 14
    "use-ffcall",
#define INFO_USE_LIBFFCALL 15
    "use-libffcall",
#define INFO_USE_LIBFFI 16
    "use-libffi",
#define INFO_USE_LIBFFI_RAW 17
    "use-libffi-raw",
#define INFO_NULL 18
    "NULL",
    NULL
  };

  if (objc < minargs) {
    Jim_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
    return JIM_ERR;
  }

  i = Jim_FindByName(Jim_GetString(objv[option_ix], NULL), options, sizeof(options) / sizeof(char*) - 1);

  switch (i) {
  case INFO_CALLOUTS:		/* return list of callout names */
    table = &client->callouts;
  list_table_keys:		/* list the keys in a hash table */
    if (objc != 2) {
      Jim_WrongNumArgs(interp,2,objv,"");
      return JIM_ERR;
    }
    iter = Jim_GetHashTableIterator(table);
    while ((entry = Jim_NextHashEntry(iter))) {
      Jim_ListAppendElement(interp, Jim_GetResult(interp), Jim_NewStringObj(interp, Jim_GetHashEntryKey(entry),-1));
    }
    Jim_Free(iter);
    return JIM_OK;
  case INFO_TYPEDEFS:		/* return list of typedef names */
    table = &client->types;
    goto list_table_keys;
  case INFO_SIGNATURES:		/* return list of ffi signatures */
    table = &client->cifs;
    goto list_table_keys;
  case INFO_LIBRARIES:		/* return list of lib names */
    table = &client->libs;
    goto list_table_keys;
  case INFO_CALLBACKS:		/* return list of callback names */
#if USE_CALLBACKS
    table = &client->callbacks;
    goto list_table_keys;
#else
    AppendResult(interp, "callbacks are not supported in this configuration", NULL);
    return JIM_ERR;
#endif

  case INFO_SIZEOF:		/* return sizeof type */
  case INFO_ALIGNOF:		/* return alignof type */
  case INFO_FORMAT:		/* return binary format of type */
    if (objc != 3) {
      Jim_WrongNumArgs(interp,2,objv,"type");
      return JIM_ERR;
    }
    arg = Jim_GetString(objv[2], NULL);
    type = type_lookup(client, arg);
    if (type == NULL) {
      AppendResult(interp, "undefined type: ", arg, NULL);
      return JIM_ERR;
    }
    if (i == INFO_SIZEOF) {
      Jim_SetResultInt(interp, type->size);
      return JIM_OK;
    }
    if (i == INFO_ALIGNOF) {
      Jim_SetResultInt(interp, type->alignment);
      return JIM_OK;
    }
    if (i == INFO_FORMAT) {
      i = 0;
      return type_format(interp, type, &i);
    }
    AppendResult(interp, "lost in ::ffidl::info?", NULL);
    return JIM_ERR;
  case INFO_INTERP:
    /* return the interp as integer */
    if (objc != 2) {
      Jim_WrongNumArgs(interp,2,objv,"");
      return JIM_ERR;
    }
    Jim_SetResult(interp, Ffidl_NewPointerObj(interp, interp));
    return JIM_OK;
  case INFO_USE_FFCALL:
  case INFO_USE_LIBFFCALL:
#if USE_LIBFFCALL
    Jim_SetResultInt(interp, 1);
#else
    Jim_SetResultInt(interp, 0);
#endif
    return JIM_OK;
  case INFO_USE_LIBFFI:
#if USE_LIBFFI
    Jim_SetResultInt(interp, 1);
#else
    Jim_SetResultInt(interp, 0);
#endif
    return JIM_OK;
  case INFO_USE_CALLBACKS:
#if USE_CALLBACKS
    Jim_SetResultInt(interp, 1);
#else
    Jim_SetResultInt(interp, 0);
#endif
    return JIM_OK;
  case INFO_HAVE_LONG_LONG:
#if HAVE_LONG_LONG
    Jim_SetResultInt(interp, 1);
#else
    Jim_SetResultInt(interp, 0);
#endif
    return JIM_OK;
  case INFO_HAVE_LONG_DOUBLE:
#if HAVE_LONG_DOUBLE
    Jim_SetResultInt(interp, 1);
#else
    Jim_SetResultInt(interp, 0);
#endif
    return JIM_OK;
  case INFO_USE_LIBFFI_RAW:
#if USE_LIBFFI_RAW_API
    Jim_SetResultInt(interp, 1);
#else
    Jim_SetResultInt(interp, 0);
#endif
    return JIM_OK;
  case INFO_HAVE_INT64:
#if HAVE_INT64
    Jim_SetResultInt(interp, 1);
#else
    Jim_SetResultInt(interp, 0);
#endif
    return JIM_OK;
  case INFO_CANONICAL_HOST:
    Jim_SetResultString(interp, CANONICAL_HOST,-1);
    return JIM_OK;
  case INFO_NULL:
    Jim_SetResult(interp, Ffidl_NewPointerObj(interp, NULL));
    return JIM_OK;
  }
  
  /* return an error */
  AppendResult(interp, "missing option implementation: ", Jim_GetString(objv[option_ix], NULL), NULL);
  return JIM_ERR;
}

/* usage: ffidl-typedef name type1 ?type2 ...? */
static int tcl_ffidl_typedef(Jim_Interp *interp, int objc, Jim_Obj *CONST objv[])
{
  enum {
    command_ix,
    name_ix,
    type_ix,
    minargs
  };

  const char *tname1, *tname2;
  ffidl_type *newtype, *ttype2;
  int nelts, i;
  ffidl_client *client = (ffidl_client *)Jim_CmdPrivData(interp);

  /* check number of args */
  if (objc < minargs) {
    Jim_WrongNumArgs(interp,1,objv,"name type ?...?");
    return JIM_ERR;
  }
  /* fetch new type name, verify that it is new */
  tname1 = Jim_GetString(objv[name_ix], NULL);
  if (type_lookup(client, tname1) != NULL) {
    AppendResult(interp, "type is already defined: ", tname1, NULL);
    return JIM_ERR;
  }
  nelts = objc - 2;
  if (nelts == 1) {
    /* define tname1 as an alias for tname2 */
    tname2 = Jim_GetString(objv[type_ix], NULL);
    ttype2 = type_lookup(client, tname2);
    if (ttype2 == NULL) {
      AppendResult(interp, "undefined type: ", tname2, NULL);
      return JIM_ERR;
    }
    /* define alias */
    type_define(client, tname1, ttype2);
    type_inc_ref(ttype2);
  } else {
    /* allocate an aggregate type */
    newtype = type_alloc(client, nelts);
    if (newtype == NULL) {
      AppendResult(interp, "couldn't allocate the ffi_type", NULL);
      return JIM_ERR;
    }
    /* parse aggregate types */
    newtype->size = 0;
    newtype->alignment = 0;
    for (i = 0; i < nelts; i += 1) {
      tname2 = Jim_GetString(objv[type_ix+i], NULL);
      ttype2 = type_lookup(client, tname2);
      if (ttype2 == NULL) {
	type_free(newtype);
	AppendResult(interp, "undefined element type: ", tname2, NULL);
	return JIM_ERR;
      }
      if ((ttype2->class & FFIDL_ELT) == 0) {
	type_free(newtype);
	AppendResult(interp, "type ", tname2, " is not permitted in element context", NULL);
	return JIM_ERR;
      }
      newtype->elements[i] = ttype2;
      /* accumulate the aggregate size and alignment */
      /* align current size to element's alignment */
      if ((ttype2->alignment-1) & newtype->size) {
	newtype->size = ((newtype->size-1) | (ttype2->alignment-1)) + 1;
      }
      /* add the element's size */
      newtype->size += ttype2->size;
      /* bump the aggregate alignment as required */
      if (ttype2->alignment > newtype->alignment) {
	newtype->alignment = ttype2->alignment;
      }
    }
    newtype->size = ((newtype->size-1) | (newtype->alignment-1)) + 1; /* tail padding as in libffi */
    if (type_prep(newtype) != JIM_OK) {
      type_free(newtype);
      AppendResult(interp, "type definition error", NULL);
      return JIM_ERR;
    }
    /* define new type */
    type_define(client, tname1, newtype);
    type_inc_ref(newtype);
  }
  /* return success */
  return JIM_OK;
}

/* usage: depends on the signature defining the ffidl-callout */
static int tcl_ffidl_call(Jim_Interp *interp, int objc, Jim_Obj *CONST objv[])
{
  enum {
    command_ix,
    args_ix,
    minargs = args_ix
  };

  ffidl_callout *callout = (ffidl_callout *)Jim_CmdPrivData(interp);
  ffidl_cif *cif = callout->cif;
  int i, itmp;
  long ltmp;
  double dtmp;
#if HAVE_INT64
  Ffidl_Int64 wtmp;
#endif
  Jim_Obj *obj = NULL;
  char buff[128];

  /* usage check */
  if (objc-args_ix != cif->argc) {
    Jim_WrongNumArgs(interp, 1, objv, callout->usage);
    return JIM_ERR;
  }
  /* fetch and convert argument values */
  for (i = 0; i < cif->argc; i += 1) {
    /* fetch object */
    obj = objv[args_ix+i];
    /* fetch value from object and store value into arg value array */
    if (cif->atypes[i]->class & FFIDL_GETINT) {
      if (obj->typePtr == ffidl_double_ObjType) {
	if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR)
	  goto cleanup;
	ltmp = (long)dtmp;
	if (dtmp != ltmp)
	  if (Jim_GetLong(interp, obj, &ltmp) == JIM_ERR)
	    goto cleanup;
      } else if (Jim_GetLong(interp, obj, &ltmp) == JIM_ERR)
	goto cleanup;
#if HAVE_INT64
    } else if (cif->atypes[i]->class & FFIDL_GETWIDEINT) {
      if (obj->typePtr == ffidl_double_ObjType) {
	if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR)
	  goto cleanup;
	wtmp = (Ffidl_Int64)dtmp;
	if (dtmp != wtmp) {
	  if (Ffidl_GetInt64FromObj(interp, obj, &wtmp) == JIM_ERR) {
	    goto cleanup;
	  }
	}
      } else if (Ffidl_GetInt64FromObj(interp, obj, &wtmp) == JIM_ERR) {
	goto cleanup;
      }
#endif
    } else if (cif->atypes[i]->class & FFIDL_GETDOUBLE) {
      if (obj->typePtr == ffidl_int_ObjType) {
	if (Jim_GetLong(interp, obj, &ltmp) == JIM_ERR)
	  goto cleanup;
	dtmp = (double)ltmp;
	if (dtmp != ltmp)
	  if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR)
	    goto cleanup;
#if HAVE_WIDE_INT
      } else if (obj->typePtr == ffidl_wideInt_ObjType) {
	if (Jim_GetWideIntFromObj(interp, obj, &wtmp) == JIM_ERR)
	  goto cleanup;
	dtmp = (double)wtmp;
	if (dtmp != wtmp)
	  if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR)
	    goto cleanup;
#endif
      } else if (Jim_GetDouble(interp, obj, &dtmp) == JIM_ERR)
	goto cleanup;
    }
    switch (cif->atypes[i]->typecode) {
    case FFIDL_INT:
      *(int *)callout->args[i] = (int)ltmp;
      continue;
    case FFIDL_FLOAT:
      *(float *)callout->args[i] = (float)dtmp;
      continue;
    case FFIDL_DOUBLE:
      *(double *)callout->args[i] = (double)dtmp;
      continue;
#if HAVE_LONG_DOUBLE
    case FFIDL_LONGDOUBLE:
      *(long double *)callout->args[i] = (long double)dtmp;
      continue;
#endif
    case FFIDL_UINT8:
      *(UINT8_T *)callout->args[i] = (UINT8_T)ltmp;
      continue;
    case FFIDL_SINT8:
      *(SINT8_T *)callout->args[i] = (SINT8_T)ltmp;
      continue;
    case FFIDL_UINT16:
      *(UINT16_T *)callout->args[i] = (UINT16_T)ltmp;
      continue;
    case FFIDL_SINT16:
      *(SINT16_T *)callout->args[i] = (SINT16_T)ltmp;
      continue;
    case FFIDL_UINT32:
      *(UINT32_T *)callout->args[i] = (UINT32_T)ltmp;
      continue;
    case FFIDL_SINT32:
      *(SINT32_T *)callout->args[i] = (SINT32_T)ltmp;
      continue;
#if HAVE_INT64
    case FFIDL_UINT64:
      *(UINT64_T *)callout->args[i] = (UINT64_T)wtmp;
      continue;
    case FFIDL_SINT64:
      *(SINT64_T *)callout->args[i] = (SINT64_T)wtmp;
      continue;
#endif
    case FFIDL_STRUCT:
      if (obj->typePtr != ffidl_bytearray_ObjType) {
	sprintf(buff, "parameter %d must be a binary string", i);
	AppendResult(interp, buff, NULL);
	goto cleanup;
      }
      callout->args[i] = obj->bytes;
      itmp = obj->length;
      if (itmp != cif->atypes[i]->size) {
	sprintf(buff, "parameter %d is the wrong size, %u bytes instead of %lu.", i, itmp, (long)(cif->atypes[i]->size));
	AppendResult(interp, buff, NULL);
	goto cleanup;
      }
      continue;
    case FFIDL_PTR:
#if FFIDL_POINTER_IS_LONG
      *(void **)callout->args[i] = (void *)ltmp;
#else
      *(void **)callout->args[i] = (void *)wtmp;
#endif
      continue;
    case FFIDL_PTR_OBJ:
      *(void **)callout->args[i] = (void *)obj;
      continue;
    case FFIDL_PTR_UTF8:
      *(void **)callout->args[i] = (void *)Jim_GetString(obj, NULL);
      continue;
/* UTF16 not supported yet in the Jim port.  revisit.
    this will involve calls to libiconv and/or Jim's utf8_tounicode().  
    this would be used by Win32 and Java.
    case FFIDL_PTR_UTF16:
      *(void **)callout->args[i] = (void *)Jim_GetUnicode(obj);
      continue;  */
    case FFIDL_PTR_BYTE:
      if (obj->typePtr != ffidl_bytearray_ObjType) {
	sprintf(buff, "parameter %d must be a binary string", i);
	AppendResult(interp, buff, NULL);
	goto cleanup;
      }
      *(void **)callout->args[i] = (void *)obj->bytes;
      continue;
    case FFIDL_PTR_VAR:
      obj = Jim_GetVariable(interp, objv[args_ix + i], JIM_ERRMSG);
      if (obj == NULL) return JIM_ERR;
      if (obj->typePtr != ffidl_bytearray_ObjType) {
	sprintf(buff, "parameter %d must be a binary string", i);
	AppendResult(interp, buff, NULL);
	goto cleanup;
      }
      if (Jim_IsShared(obj)) {
	itmp = Jim_SetVariable(interp, objv[args_ix+i], Jim_DuplicateObj(interp, obj));
	if (itmp != JIM_OK) {
	  goto cleanup;
	}
      }
      *(void **)callout->args[i] = (void *)obj->bytes;
      /* printf("pointer-var -> %d\n", cif->avalues[i].v_pointer); */
      Jim_InvalidateStringRep(obj);
      continue;
#if USE_CALLBACKS
    case FFIDL_PTR_PROC: {
      ffidl_callback *callback;
      ffidl_closure *closure;
      Jim_DString ds;
      const char *name = Jim_GetString(objv[args_ix+i], NULL);
      Jim_DStringInit(&ds);
      if (!strstr(name, "::")) {
        /* 'name' is relative.  resolve it to an absolute namespace instead. */
        Jim_Obj *ns;
        ns = interp->framePtr->nsObj;
        if (ns != interp->topFramePtr->nsObj) {
          /* interp's current namespace is not its global one.  prepend that namespace. */
          Jim_DStringAppend(&ds, Jim_GetString(ns, NULL), -1);
        }
        Jim_DStringAppend(&ds, "::", 2);
        Jim_DStringAppend(&ds, name, -1);
        name = Jim_DStringValue(&ds);
      }
      callback = callback_lookup(callout->client, name);
      Jim_DStringFree(&ds);
      if (callback == NULL) {
	AppendResult(interp, "no callback named \"", Jim_GetString(objv[args_ix+i], NULL), "\" is defined", NULL);
	goto cleanup;
      }
      closure = &(callback->closure);
#if USE_LIBFFI
      *(void **)callout->args[i] = (void *)closure->executable;
#elif USE_LIBFFCALL
      *(void **)callout->args[i] = (void *)closure->lib_closure;
#endif
    }
    continue;
#endif
    default:
      sprintf(buff, "unknown type for argument: %d", cif->atypes[i]->typecode);
      AppendResult(interp, buff, NULL);
      goto cleanup;
    }
    /* Note: change "continue" to "break" if further work must be done here. */
  }
  /* prepare for structure return */
  if (cif->rtype->typecode == FFIDL_STRUCT) {
    obj = Jim_NewStringObj(interp, NULL, 0);
    obj->length = cif->rtype->size;
    obj->bytes = Jim_Alloc(obj->length);
    callout->ret = obj->bytes;
    Jim_IncrRefCount(obj);
  }
  /* call */
  callout_call(callout);
  /* convert return value */
  switch (cif->rtype->typecode) {
  case FFIDL_VOID:	break;
  case FFIDL_INT:	Jim_SetResult(interp, Jim_NewLongObj(interp, (long)FFIDL_RVALUE_PEEK_UNWIDEN(INT, callout->ret))); break;
  case FFIDL_FLOAT:	Jim_SetResult(interp, Jim_NewDoubleObj(interp, (double)FFIDL_RVALUE_PEEK_UNWIDEN(FLOAT, callout->ret))); break;
  case FFIDL_DOUBLE:	Jim_SetResult(interp, Jim_NewDoubleObj(interp, (double)FFIDL_RVALUE_PEEK_UNWIDEN(DOUBLE, callout->ret))); break;
#if HAVE_LONG_DOUBLE
  case FFIDL_LONGDOUBLE:Jim_SetResult(interp, Jim_NewDoubleObj(interp, (double)FFIDL_RVALUE_PEEK_UNWIDEN(LONGDOUBLE, callout->ret))); break;
#endif
  case FFIDL_UINT8:	Jim_SetResult(interp, Jim_NewLongObj(interp, (long)FFIDL_RVALUE_PEEK_UNWIDEN(UINT8, callout->ret))); break;
  case FFIDL_SINT8:	Jim_SetResult(interp, Jim_NewLongObj(interp, (long)FFIDL_RVALUE_PEEK_UNWIDEN(SINT8, callout->ret))); break;
  case FFIDL_UINT16:	Jim_SetResult(interp, Jim_NewLongObj(interp, (long)FFIDL_RVALUE_PEEK_UNWIDEN(UINT16, callout->ret))); break;
  case FFIDL_SINT16:	Jim_SetResult(interp, Jim_NewLongObj(interp, (long)FFIDL_RVALUE_PEEK_UNWIDEN(SINT16, callout->ret))); break;
  case FFIDL_UINT32:	Jim_SetResult(interp, Jim_NewLongObj(interp, (long)FFIDL_RVALUE_PEEK_UNWIDEN(UINT32, callout->ret))); break;
  case FFIDL_SINT32:	Jim_SetResult(interp, Jim_NewLongObj(interp, (long)FFIDL_RVALUE_PEEK_UNWIDEN(SINT32, callout->ret))); break;
#if HAVE_INT64
  case FFIDL_UINT64:	Jim_SetResult(interp, Ffidl_NewInt64Obj(interp, (Ffidl_Int64)FFIDL_RVALUE_PEEK_UNWIDEN(UINT64, callout->ret))); break;
  case FFIDL_SINT64:	Jim_SetResult(interp, Ffidl_NewInt64Obj(interp, (Ffidl_Int64)FFIDL_RVALUE_PEEK_UNWIDEN(SINT64, callout->ret))); break;
#endif
  case FFIDL_STRUCT:	Jim_SetResult(interp, obj); Jim_DecrRefCount(interp, obj); break;
  case FFIDL_PTR:	Jim_SetResult(interp, Ffidl_NewPointerObj(interp, FFIDL_RVALUE_PEEK_UNWIDEN(PTR, callout->ret))); break;
  case FFIDL_PTR_OBJ:	Jim_SetResult(interp, (Jim_Obj *)FFIDL_RVALUE_PEEK_UNWIDEN(PTR, callout->ret)); break;
  case FFIDL_PTR_UTF8:	Jim_SetResult(interp, Jim_NewStringObj(interp, FFIDL_RVALUE_PEEK_UNWIDEN(PTR, callout->ret), -1)); break;
/* UTF16 not supported yet in the Jim port.
  case FFIDL_PTR_UTF16:	Jim_SetResult(interp, Jim_NewUnicodeObj(interp, FFIDL_RVALUE_PEEK_UNWIDEN(PTR, callout->ret), -1)); break; */
  default:
    sprintf(buff, "Invalid return type: %d", cif->rtype->typecode);
    AppendResult(interp, buff, NULL);
    goto cleanup;
  }    
  /* done */
  return JIM_OK;
  /* blew it */
 cleanup:
  return JIM_ERR;
}

/* usage: ffidl-callout name {?argument_type ...?} return_type address ?protocol? */
static int tcl_ffidl_callout(Jim_Interp *interp, int objc, Jim_Obj *CONST objv[])
{
  enum {
    command_ix,
    name_ix,
    args_ix,
    return_ix,
    address_ix,
    protocol_ix,
    minargs = address_ix + 1,
    maxargs = protocol_ix + 1,
  };

  const char *name;
  void (*fn)();
  int argc, i, len;
  Jim_Obj **argv;
  Jim_DString usage, ds;
  int res;
  ffidl_cif *cif = NULL;
  ffidl_callout *callout;
  ffidl_client *client = (ffidl_client *)Jim_CmdPrivData(interp);
  int has_protocol = objc - 1 >= protocol_ix;

  /* usage check */
  if (objc != minargs && objc != maxargs) {
    Jim_WrongNumArgs(interp, 1, objv, "name {?argument_type ...?} return_type address ?protocol?");
    return JIM_ERR;
  }
  Jim_DStringInit(&ds);
  Jim_DStringInit(&usage);
  /* fetch name */
  name = Jim_GetString(objv[name_ix], NULL);
  if (!strstr(name, "::")) {
    /* 'name' is relative.  resolve it to an absolute namespace instead. */
    Jim_Obj *ns;
    ns = interp->framePtr->nsObj;
    if (ns != interp->topFramePtr->nsObj) {
      /* interp's current namespace is not its global one.  prepend that namespace. */
      Jim_DStringAppend(&ds, Jim_GetString(ns, NULL), -1);
    }
    Jim_DStringAppend(&ds, "::", 2);
    Jim_DStringAppend(&ds, name, -1);
    name = Jim_DStringValue(&ds);
  }
  /* fetch cif */
  if (cif_parse(interp, client,
		objv[args_ix],
		objv[return_ix],
		has_protocol ? objv[protocol_ix] : NULL,
		&cif) == JIM_ERR) {
    goto error;
  }
  /* fetch function pointer */
  if (Ffidl_GetPointerFromObj(interp, objv[address_ix], (void **)&fn) == JIM_ERR) {
    goto error;
  }
  /* if callout is already defined, redefine it */
  if ((callout = callout_lookup(client, name))) {
    Jim_DeleteCommand(interp, name);
  }
  /* build the usage string */
  if (SetListFromAny(interp, objv[args_ix]) != JIM_OK) goto error;
  argc = objv[args_ix]->internalRep.listValue.len;
  argv = objv[args_ix]->internalRep.listValue.ele;
  for (i = 0; i < argc; i++) {
    if (i != 0) Jim_DStringAppend(&usage, " ", 1);
    Jim_DStringAppend(&usage, Jim_GetString(argv[i], &len), len);
  }
  /* allocate the callout structure, including:
     - usage string
     - argument value pointers
     - argument values */
  callout = (ffidl_callout *)Jim_Alloc(sizeof(ffidl_callout)
				       +cif->argc*sizeof(void*) /* args */
				       +sizeof(ffidl_value)	/* rvalue */
				       +cif->argc*sizeof(ffidl_value) /* avalues */
				       +Jim_DStringLength(&usage)+1); /* usage */
  if (callout == NULL) {
    AppendResult(interp, "can't allocate ffidl_callout for: ", name, NULL);
    goto error;
  }
  /* initialize the callout */
  callout->cif = cif;
  callout->fn = fn;
  callout->client = client;
  /* set up return and argument pointers */
  callout->args = (void **)(callout+1);
  ffidl_value *rvalue = (ffidl_value *)(callout->args+cif->argc);
  ffidl_value *avalues = (ffidl_value *)(rvalue+1);
  /* prep return value */
  if (callout_prep_value(interp, FFIDL_RET, objv[return_ix], cif->rtype,
			 rvalue, &callout->ret) == JIM_ERR) {
    goto error;
  }
  /* prep argument values */
  for (i = 0; i < argc; i += 1) {
    if (callout_prep_value(interp, FFIDL_ARG, argv[i], cif->atypes[i],
			   &avalues[i], &callout->args[i]) == JIM_ERR) {
      goto error;
    }
  }
  callout_prep(callout);
  /* set up usage string */
  callout->usage = (char *)((avalues+cif->argc));
  strcpy(callout->usage, Jim_DStringValue(&usage));
  /* free the usage string */
  Jim_DStringFree(&usage);
  /* define the callout */
  callout_define(client, name, callout);
  /* create the tcl command */
  res = Jim_CreateCommand(interp, name, tcl_ffidl_call, (ClientData) callout, callout_delete);
  Jim_DStringFree(&ds);
  return res;
error:
  Jim_DStringFree(&ds);
  Jim_DStringFree(&usage);
  if (cif) {
    cif_dec_ref(cif);
  }
  return JIM_ERR;
}

#if USE_CALLBACKS
/* usage: ffidl-callback name {?argument_type ...?} return_type ?protocol? ?cmdprefix? -> */
static int tcl_ffidl_callback(Jim_Interp *interp, int objc, Jim_Obj *CONST objv[])
{
  enum {
    command_ix,
    name_ix,
    args_ix,
    return_ix,
    protocol_ix,
    cmdprefix_ix,
    minargs = return_ix + 1,
    maxargs = cmdprefix_ix + 1,
  };

  const char *name;
  Jim_Obj *nameObj;
  ffidl_cif *cif = NULL;
  Jim_Obj **cmdv = NULL;
  int cmdc;
  Jim_DString ds;
  ffidl_callback *callback = NULL;
  ffidl_client *client = (ffidl_client *)Jim_CmdPrivData(interp);
  ffidl_closure *closure = NULL;
  void (*fn)();
  int has_protocol = objc - 1 >= protocol_ix;
  int has_cmdprefix = objc - 1 >= cmdprefix_ix;
  int i, argc = 0;

  /* usage check */
  if (objc < minargs || objc > maxargs) {
    Jim_WrongNumArgs(interp, 1, objv, "name {?argument_type ...?} return_type ?protocol? ?cmdprefix?");
    return JIM_ERR;
  }
  /* fetch name */
  Jim_DStringInit(&ds);
  name = Jim_GetString(objv[name_ix], NULL);
  if (!strstr(name, "::")) {
    /* 'name' is relative.  resolve it to an absolute namespace instead. */
    Jim_Obj *ns;
    ns = interp->framePtr->nsObj;
    if (ns != interp->topFramePtr->nsObj) {
      /* interp's current namespace is not its global one.  prepend that namespace. */
      Jim_DStringAppend(&ds, Jim_GetString(ns, NULL), -1);
    }
    Jim_DStringAppend(&ds, "::", 2);
    Jim_DStringAppend(&ds, name, -1);
    name = Jim_DStringValue(&ds);
  }
  /* fetch cif */
  if (cif_parse(interp, client,
		objv[args_ix],
		objv[return_ix],
		has_protocol ? objv[protocol_ix] : NULL,
		&cif) == JIM_ERR) {
      goto error;
  }
  /* check types */
  if (cif_type_check_context(interp, FFIDL_CBRET,
			     objv[return_ix], cif->rtype) == JIM_ERR) {
    goto error;
  }
  if (SetListFromAny(interp, objv[args_ix]) != JIM_OK) goto error;
  argc = objv[args_ix]->internalRep.listValue.len;
  for (i = 0; i < argc; i += 1)
    if (cif_type_check_context(interp, FFIDL_ARG,
			       objv[args_ix], cif->atypes[i]) == JIM_ERR) {
      goto error;
    }
  /* create Tcl proc */
  if (has_cmdprefix) {
    Jim_Obj *cmdprefix = objv[cmdprefix_ix];
    Jim_IncrRefCount(cmdprefix);
    if (SetListFromAny(interp, cmdprefix) != JIM_OK) goto error;
    cmdc = cmdprefix->internalRep.listValue.len;
    cmdv = cmdprefix->internalRep.listValue.ele;
    for (i = 0; i < cmdc; i++) {
      Jim_IncrRefCount(cmdv[i]);
    }
    Jim_DecrRefCount(interp, cmdprefix);
  } else {
    /* the callback name is the command */
    nameObj = Jim_NewStringObj(interp, name, -1);
    cmdv = &nameObj;
    cmdc = 1;
    Jim_IncrRefCount( nameObj);
  }

  /* allocate the callback structure */
  callback = (ffidl_callback *)Jim_Alloc(sizeof(ffidl_callback)
					 /* cmdprefix and argument Jim_Objs */
					 +(cmdc+cif->argc)*sizeof(Jim_Obj *)
#if USE_LIBFFI_RAW_API
					 /* raw argument offsets */
					 +cif->argc*sizeof(ptrdiff_t)
#endif
  );
  if (callback == NULL) {
    AppendResult(interp, "can't allocate ffidl_callback for: ", name, NULL);
    goto error;
  }
  /* initialize the callback */
  callback->cif = cif;
  callback->interp = interp;
  /* store the command prefix' Jim_Objs */
  callback->cmdc = cmdc;
  callback->cmdv = (Jim_Obj **)(callback+1);
  memcpy(callback->cmdv, cmdv, cmdc*sizeof(Jim_Obj *));
  closure = &(callback->closure);
#if USE_LIBFFI
  closure->lib_closure = ffi_closure_alloc(sizeof(ffi_closure), &(closure->executable));
#if USE_LIBFFI_RAW_API
  callback->offsets = (ptrdiff_t *)(callback->cmdv+cmdc+cif->argc);
  callback->use_raw_api = cif_raw_supported(cif);
  callback->use_raw_api = 0;
  if (callback->use_raw_api &&
      ffi_prep_raw_closure_loc((ffi_raw_closure *)closure->lib_closure, &callback->cif->lib_cif,
				 (void (*)(ffi_cif*,void*,ffi_raw*,void*))callback_callback,
                                 (void *)callback, closure->executable) == FFI_OK) {
    if (JIM_OK != cif_raw_prep_offsets(callback->cif, callback->offsets)) {
      goto error;
    }
    /* Prepared successfully, continue. */
  }
  else
#endif
  {
#if USE_LIBFFI_RAW_API
    callback->use_raw_api = 0;
#endif
    if (ffi_prep_closure_loc(closure->lib_closure, &callback->cif->lib_cif,
                            (void (*)(ffi_cif*,void*,void**,void*))callback_callback,
                            (void *)callback, closure->executable) != FFI_OK) {
      AppendResult(interp, "libffi can't make closure for: ", name, NULL);
      goto error;
    }
  }
#elif USE_LIBFFCALL
  closure->lib_closure = alloc_callback((callback_function_t)&callback_callback,
					(void *)callback);
#endif
  /* define the callback */
  callback_define(client, name, callback);
  Jim_DStringFree(&ds);

  /* Return function pointer to the callback. */
#if USE_LIBFFI
  fn = (void (*)())closure->executable;
#elif USE_LIBFFCALL
  fn = (void (*)())closure->lib_closure;
#endif
  Jim_SetResult(interp, Ffidl_NewPointerObj(interp, fn));

  return JIM_OK;

error:
  Jim_DStringFree(&ds);
  if (cif) {
    cif_dec_ref(cif);
  }
  if (cmdv) {
    for (i = 0; i < cmdc; i++) {
      Jim_DecrRefCount(interp, cmdv[i]);
    }
  }
  if (closure && closure->lib_closure) {
#if USE_LIBFFI
    ffi_closure_free(closure->lib_closure);
#elif USE_LIBFFCALL
    free_callback(closure->lib_closure);
#endif
  }
  if (callback) {
      Jim_Free((void *)callback);
  }
  return JIM_ERR;
}
#endif

/* usage: ffidl::library library ?options...?*/
static int tcl_ffidl_library(Jim_Interp *interp, int objc, Jim_Obj *CONST objv[])
{
  enum {
    command_ix,
    optional_ix,
    minargs
  };

   static const char *options[] = {
      "-binding",
      "-visibility",
      "--",
      NULL,
   };

  enum {
    option_binding,
    option_visibility,
    option_break,
  };

   static const char *bindingOptions[] = {
      "now",
      "lazy",
      NULL,
   };

   enum {
     binding_now,
     binding_lazy,
   };

   static const char *visibilityOptions[] = {
      "global",
      "local",
      NULL,
   };

  enum {
    visibility_global,
    visibility_local,
  };

  int i = 0;
  ffidl_load_flags flags = {FFIDL_LOAD_BINDING_NONE, FFIDL_LOAD_VISIBILITY_NONE};
  Jim_Obj *libraryObj;
  const char *libraryName;
  ffidl_LoadHandle handle;
  ffidl_UnloadProc unload;
  ffidl_client *client = (ffidl_client *)Jim_CmdPrivData(interp);

  if (objc < minargs) {
    Jim_WrongNumArgs(interp, 1, objv, "?flags? ?--? library");
    return JIM_ERR;
  }

  for (i = optional_ix; i < objc; ++i) {
      int option = Jim_FindByName(Jim_GetString(objv[i], NULL), options, 
        sizeof(options)/sizeof(char*) - 1);
      if (option == -1) {
        /* No options. */
        Jim_SetEmptyResult(interp);
        AppendResult(interp, "bad option: ", Jim_GetString(objv[i], NULL), NULL);
        break;
      }

      if (option_break == option) {
        /* End of options. */
        i++;
        break;
      }

      switch (option) {
	case option_binding:
	{
	  i++;
      int bindingOption = Jim_FindByName(Jim_GetString(objv[i], NULL), bindingOptions, 
        sizeof(bindingOptions)/sizeof(char*) - 1);
      if (bindingOption == -1) {
        AppendResult(interp, "bad option: ", Jim_GetString(objv[i], NULL), NULL);
	    return JIM_ERR;
	  }

	  switch (bindingOption) {
	    case binding_lazy:
	      flags.binding = FFIDL_LOAD_BINDING_LAZY;
	      break;
	    case binding_now:
	      flags.binding = FFIDL_LOAD_BINDING_NOW;
	      break;
	  }
	  break;
	}
	case option_visibility:
	{
	  i++;
	  int visibilityOption = Jim_FindByName(Jim_GetString(objv[i], NULL), visibilityOptions, 
        sizeof(visibilityOptions)/sizeof(char*) - 1);
	  if (visibilityOption == -1) {
        AppendResult(interp, "bad option: ", Jim_GetString(objv[i], NULL), NULL);
	    return JIM_ERR;
	  }

	  switch (visibilityOption) {
	    case visibility_global:
	      flags.visibility = FFIDL_LOAD_VISIBILITY_GLOBAL;
	      break;
	    case visibility_local:
	      flags.visibility = FFIDL_LOAD_VISIBILITY_LOCAL;
	      break;
	  }
	  break;
	}
	case option_break:
	  /* Already handled above */
	  break;
      }
  }

  libraryObj = objv[i];
  libraryName = Jim_GetString(libraryObj, NULL);
  handle = lib_lookup(client, libraryName, NULL);

  if (handle != NULL) {
    AppendResult(interp, "library \"", libraryName, "\" already loaded", NULL);
    return JIM_ERR;
  }

  if (ffidlopen(interp, libraryObj, flags, &handle, &unload) != JIM_OK) {
    return JIM_ERR;
  }

  lib_define(client, libraryName, handle, unload);

  return JIM_OK;
}

/* usage: ffidl-symbol library symbol -> address */
static int tcl_ffidl_symbol(Jim_Interp *interp, int objc, Jim_Obj *CONST objv[])
{
  enum {
    command_ix,
    library_ix,
    symbol_ix,
    nargs
  };

  const char *library;
  void *address;
  ffidl_LoadHandle handle;
  ffidl_UnloadProc unload;
  ffidl_client *client = (ffidl_client *)Jim_CmdPrivData(interp);

  if (objc != nargs) {
    Jim_WrongNumArgs(interp,1,objv,"library symbol");
    return JIM_ERR;
  }

  library = Jim_GetString(objv[library_ix], NULL);
  handle = lib_lookup(client, library, NULL);

  if (handle == NULL) {
    ffidl_load_flags flags = {FFIDL_LOAD_BINDING_NONE, FFIDL_LOAD_VISIBILITY_NONE};
    if (ffidlopen(interp, objv[library_ix], flags, &handle, &unload) != JIM_OK) {
      return JIM_ERR;
    }
    lib_define(client, library, handle, unload);
  }

  if (ffidlsym(interp, handle, objv[symbol_ix], &address) != JIM_OK) {
    return JIM_ERR;
  }

  Jim_SetResult(interp, Ffidl_NewPointerObj(interp, address));
  return JIM_OK;
}

/* usage: ffidl-stubsymbol library stubstable symbolnumber -> address */
static int tcl_ffidl_stubsymbol(Jim_Interp *interp, int objc, Jim_Obj *CONST objv[])
{
  enum {
    command_ix,
    library_ix,
    stubstable_ix,
    symbol_ix,
    nargs
  };

  int library, stubstable, symbolnumber; 
  long lg;
  void **stubs = NULL, *address;
  static const char *library_names[] = {
    "tcl", 
#if defined(LOOKUP_TK_STUBS)
    "tk",
#endif
    NULL
  };
  enum libraries {
    LIB_TCL, LIB_TK,
  };
  static const char *stubstable_names[] = {
    "stubs", "intStubs", "platStubs", "intPlatStubs", "intXLibStubs", NULL
  };
  enum stubstables {
    STUBS, INTSTUBS, PLATSTUBS, INTPLATSTUBS, INTXLIBSTUBS,
  };

  if (objc != 4) {
    Jim_WrongNumArgs(interp,1,objv,"library stubstable symbolnumber");
    return JIM_ERR;
  }
  library = Jim_FindByName(Jim_GetString(objv[library_ix], NULL), library_names, 
    sizeof(library_names) / sizeof(char*) - 1);
  if (library == -1) {
    AppendResult(interp, "library not found: ", Jim_GetString(objv[library_ix], NULL), NULL);
    return JIM_ERR;
  }
  stubstable = Jim_FindByName(Jim_GetString(objv[stubstable_ix], NULL), stubstable_names, 
    sizeof(stubstable_names) / sizeof(char*) - 1);
  if (stubstable == -1) {
    AppendResult(interp, "stubs table not found: ", Jim_GetString(objv[stubstable_ix], NULL), NULL);
    return JIM_ERR;
  }
  if (Jim_GetLong(interp, objv[symbol_ix], &lg) != JIM_OK || lg < 0) {
    return JIM_ERR;
  }
  symbolnumber = lg;

#if defined(LOOKUP_TK_STUBS)
  if (library == LIB_TK) {
    if (MyTkInitStubs(interp, "8.4", 0) == NULL) {
      return JIM_ERR;
    }
  }
#endif
  /* not yet supported in the Jim port. 
  switch (stubstable) {
    case STUBS:
      stubs = (void**)(library == LIB_TCL ? tclStubsPtr : tkStubsPtr); break;
    case INTSTUBS:
      stubs = (void**)(library == LIB_TCL ? tclIntStubsPtr : tkIntStubsPtr); break;
    case PLATSTUBS:
      stubs = (void**)(library == LIB_TCL ? tclPlatStubsPtr : tkPlatStubsPtr); break;
    case INTPLATSTUBS:
      stubs = (void**)(library == LIB_TCL ? tclIntPlatStubsPtr : tkIntPlatStubsPtr); break;
    case INTXLIBSTUBS:
      stubs = (void**)(library == LIB_TCL ? NULL : tkIntXlibStubsPtr); break;
  } */
  stubs = NULL;

  if (!stubs) {
    AppendResult(interp, "no stubs table \"", Jim_GetString(objv[stubstable_ix], NULL),
        "\" in library \"", Jim_GetString(objv[library_ix], NULL), "\"", NULL);
    return JIM_ERR;
  }
  address = *(stubs + 2 + symbolnumber);
  if (!address) {
    AppendResult(interp, "couldn't find symbol number ", Jim_GetString(objv[symbol_ix], NULL),
        " in stubs table \"", Jim_GetString(objv[stubstable_ix], NULL), "\"", NULL);
    return JIM_ERR;
  }

  Jim_SetResult(interp, Ffidl_NewPointerObj(interp, address));
  return JIM_OK;
}

/*
 * One function exported for pointer punning with ffidl-callout.
 */
void *ffidl_pointer_pun(void *p) { return p; }

/* this function's name is based on the library's actual filename.  Jim requires that. */
int Jim_FfidlJimInit(Jim_Interp *interp)
{
  ffidl_client *client;

  /* not yet supported in the Jim port. 
  if (Jim_InitStubs(interp, "8.4", 0) == NULL) {
    return JIM_ERR;
  }
  if (Jim_PkgRequire(interp, "Tcl", "8.4", 0) == NULL) {
      return JIM_ERR;
  } */
  if (Jim_PackageProvide(interp, "Ffidl", PACKAGE_VERSION, 0) != JIM_OK) {
    return JIM_ERR;
  }

  /* allocate and initialize client for this interpreter */
  client = client_alloc(interp);

  /* initialize commands */
  Jim_CreateCommand(interp,"::ffidl::info", tcl_ffidl_info, (ClientData) client, NULL);
  Jim_CreateCommand(interp,"::ffidl::typedef", tcl_ffidl_typedef, (ClientData) client, NULL);
  Jim_CreateCommand(interp,"::ffidl::library", tcl_ffidl_library, (ClientData) client, NULL);
  Jim_CreateCommand(interp,"::ffidl::symbol", tcl_ffidl_symbol, (ClientData) client, NULL);
  Jim_CreateCommand(interp,"::ffidl::stubsymbol", tcl_ffidl_stubsymbol, (ClientData) client, NULL);
  Jim_CreateCommand(interp,"::ffidl::callout", tcl_ffidl_callout, (ClientData) client, NULL);
#if USE_CALLBACKS
  Jim_CreateCommand(interp,"::ffidl::callback", tcl_ffidl_callback, (ClientData) client, NULL);
#endif

  /* determine Jim_ObjType * for some types */
  Jim_Obj * temp;
  temp = Jim_NewStringObj(interp, "", 0);
  ffidl_bytearray_ObjType = temp->typePtr;
  Jim_FreeObj(interp, temp);
  temp = Jim_NewIntObj(interp, 0);
  ffidl_int_ObjType = temp->typePtr;
  Jim_FreeObj(interp, temp);
#if HAVE_WIDE_INT
  /* not supported by Jim.
  ffidl_wideInt_ObjType = Jim_GetObjType("wideInt"); */
#endif
  temp = Jim_NewDoubleObj(interp, 0.0);
  ffidl_double_ObjType = temp->typePtr;
  Jim_FreeObj(interp, temp);

  /* done */
  return JIM_OK;
}

#if defined(LOOKUP_TK_STUBS)
typedef struct MyTkStubHooks {
    void *tkPlatStubs;
    void *tkIntStubs;
    void *tkIntPlatStubs;
    void *tkIntXlibStubs;
} MyTkStubHooks;

typedef struct MyTkStubs {
    int magic;
    struct MyTkStubHooks *hooks;
} MyTkStubs;

/* private copy of Tk_InitStubs to avoid having to depend on Tk at build time */
static const char *
MyTkInitStubs(interp, version, exact)
    Jim_Interp *interp;
    char *version;
    int exact;
{
    const char *actualVersion;

    actualVersion = Jim_PkgRequireEx(interp, "Tk", version, exact,
		(ClientData *) &tkStubsPtr);
    if (!actualVersion) {
	return NULL;
    }

    if (!tkStubsPtr) {
	Jim_SetResultString(interp, "This implementation of Tk does not support stubs", -1);
	return NULL;
    }
    
    tkPlatStubsPtr =    ((MyTkStubs*)tkStubsPtr)->hooks->tkPlatStubs;
    tkIntStubsPtr =     ((MyTkStubs*)tkStubsPtr)->hooks->tkIntStubs;
    tkIntPlatStubsPtr = ((MyTkStubs*)tkStubsPtr)->hooks->tkIntPlatStubs;
    tkIntXlibStubsPtr = ((MyTkStubs*)tkStubsPtr)->hooks->tkIntXlibStubs;
    
    return actualVersion;
}
#endif

/* Local Variables: */
/* c-basic-offset: 2 */
/* End: */

