#
# Run time support for Ffidl.
#
package provide Ffidlrt 0.2
package require Ffidl

proc ::ffidl::find-pkg-lib {pkg} {
    package require $pkg
    # [::info loaded] not supported in Jim.  revisit.
    foreach i [::info loaded {}] {
        foreach {l p} $i {}
        if {$p eq "$pkg"} {
            return $l
        }
    }
    # ignore errors when running under pkg_mkIndex:
    if {![llength [::info commands __package_orig]] } {
        return -code error "Library for package $pkg not found"
    }
}

# examine the system and return a list of its search dirs for library binaries.
proc ::ffidl::get-lib-search-paths {} {
    set dirs [list]
    # -N allows non-root users to run this command:
    set pip [open {|/sbin/ldconfig -N -v 2>/dev/null}  r]
    # /sbin/ldconfig is accessible by name on e.g. Debian 10.
    while { ! [eof $pip]} {
        set ln [string trim [gets $pip]]
        if {[string match *: $ln]} {
            lappend dirs [string range $ln 0 end-1]
        }
    }
    close $pip
    return $dirs
}

#
# search the system to find a shared library given its bare name.
# more detail can be passed in the name to reduce the likelihood of finding an unsuitable file.
# some examples (on a ficticious Linux system):
#   junk
#       resolves to:  /usr/lib/libjunk.so.3.2
#   stuff-9
#       resolves to:  /opt/local/lib/libstuff-9.so.3
#   libstuff-9.so.1
#       resolves to:  /opt/local/lib/libstuff-9.so.1
#   /home/jim/libstuff-9
#       resolves to:  /home/jim/libstuff-9.so.1
#   
# find-lib returns the absolute path to the found library, ready to load.
# returns empty string if none found.
#
proc ::ffidl::find-lib {bareName} {
    variable platUpr
    variable libs
    variable paths
    
    puts bareName=$bareName
    
    # if the given bareName contains a relative path, resolve it to absolute right away.
    if {[llength [file split $bareName]] > 1} {
        set bareName [file join [pwd] $bareName]
    }
    
    # form a list of known (magic) and/or guessed full names for this lib.
    # really each one is a glob pattern, to allow for unforeseen versions etc.
    # if one includes an absolute path, that is honored.  otherwise it's tested
    # within each dir in ::ffidl::paths.
    set names [list]
    if {[::info exists libs($bareName)] && [llength $libs($bareName)] > 0} {
        lappend names {*}$libs($bareName)
    }
    lappend names  lib${bareName}.so  lib${bareName}.so.*  \
        ${bareName}.so  ${bareName}.so.*
    puts names=$names
    
    # search for each full name pattern, in each of the system's search dirs.
    foreach n $names {
        foreach dir $::ffidl::paths {
            # if the dir contains a relative path, resolve it to absolute right away.
            set dir [file join [pwd] $dir]

            # skip any dir that probably doesn't match the machine's word size.
            if {$platUpr(WORDSIZE) == 8} {
                if [string match *32* $dir] continue
                if [string match *386* $dir] continue
                if [string match *686* $dir] continue
                # can't rule out x86 here since it appears in x86_64 arch.
            } elseif {$platUpr(WORDSIZE) == 4} {
                if [string match *64* $dir] continue
            }
            
            # find all available versions in this dir that match the name pattern.
            puts search=[file join $dir $n]
            set versions [glob -nocomplain [file join $dir $n]]
            if {[llength $versions]} {
                puts "versions=\n    [join $versions "\n    "]"
                # accept the (hopefully) latest version based on filename.
                # this sort is not foolproof, e.g. junk.so.9 newer than junk.so.10 !
                lassign [lsort -decreasing $versions] latest
                if [file readable $latest] {
                    set libs($bareName) $latest
                    return $latest
                }
            }
        }
    }
    return {}
}

#
# find a typedef for a standard type
# and define it with ::ffidl::typedef
# if not already done
#
# currently wired for my linux box
#
proc ::ffidl::find-type {type} {
    variable types
    variable typedefs 
puts types=[array get types]
puts ::ffidl::types=[array get ::ffidl::types]
puts vars=[::info vars ::*]
    if { ! [::info exists types($type)]} {
	error "::ffidl::find-type $type - no mapping defined for $type"
    }
    if { ! [::info exists typedefs($type)]} {
	eval ::ffidl::typedef $type $types($type)
	set typedefs($type) 1
    }
}
    
namespace eval ::ffidl {
    variable platUpr [string toupper $::tcl_platform]

    # 'paths' array is used by the ::ffidl::find-lib
    # abstraction to store the resolved lib paths
    variable paths {}
    set paths [::ffidl::get-lib-search-paths]

    # 'libs' array is used by the ::ffidl::find-lib
    # abstraction to store the resolved lib paths
    variable libs {} 
    set ffidl_lib ./FfidlJim.0.8b0.so
    array set libs [list ffidl $ffidl_lib ffidl_test $ffidl_lib]
    unset ffidl_lib

    # 'types' and 'typedefs' arrays are used by the ::ffidl::find-type
    # abstraction to store resolved system types
    # and whether they have already been defined
    # with ::ffidl::typedef
    variable types {} 
    variable typedefs {}
    array set typedefs {}
    switch -exact $platUpr(PLATFORM) {
	UNIX {
	    switch -glob $platUpr(OS) {
            DARWIN {
                # revisit.  eliminate as many magic libs here as possible.
                array set libs {
                    c System.framework/System
                    m System.framework/System
                    gdbm {}
                    gmp {}
                    mathswig libmathswig0.5.dylib
                }
                array set types {
                    size_t {{unsigned long}}
                    clock_t {{unsigned long}}
                    time_t long
                    timeval {uint32 uint32}
                }
            }
            LINUX {
                # previously Alpha machines had special magic here.
                # but Jim doesn't offer (MACHINE) info.
                if {$platUpr(WORDSIZE) == 8} {
                    # Linux 64-bit.
                    array set libs {
                        c {
                            libc.so.6
                        }
                        m {
                            /lib64/libm.so.6
                            /lib/x86_64-linux-gnu/libm.so.6
                        }
                        gdbm {
                            /usr/lib64/libgdbm.so
                            /usr/lib/x86_64-linux-gnu/libgdbm.so
                        }
                        gmp {
                            /usr/lib/x86_64-linux-gnu/libgmp.so
                            /usr/local/lib64/libgmp.so
                            /usr/lib64/libgmp.so.2
                        }
                        mathswig libmathswig0.5.so
                    }
                    array set types {
                        size_t long
                        clock_t long
                        time_t long
                        timeval {time_t time_t}
                    }
                } else {
                    # Linux 32-bit.
                    array set libs {
                        c {
                                /lib/libc.so.6
                                /lib/i386-linux-gnu/libc.so.6
                        }
                        m {
                                /lib/libm.so.6
                                /lib/i386-linux-gnu/libm.so.6
                        }
                        gdbm {
                                /usr/lib/libgdbm.so
                                /usr/lib/i386-linux-gnu/libgdbm.so.3
                        }
                        gmp {
                               /usr/lib/i386-linux-gnu/libgmp.so.2
                               /usr/local/lib/libgmp.so
                               /usr/lib/libgmp.so.2
                        }
                        mathswig libmathswig0.5.so
                    }
                    array set types {
                        size_t int
                        clock_t long
                        time_t long
                        timeval {time_t time_t}
                    }
                }
            }
            *BSD {
                array set libs {
                    c {/usr/lib/libc.so /usr/lib/libc.so.30.1}
                    m {/usr/lib/libm.so /usr/lib/libm.so.1.0}
                    gdbm libgdbm.so
                    gmp libgmp.so
                    mathswig libmathswig0.5.so
                }
                array set types {
                    size_t int
                    clock_t long
                    time_t long
                    timeval {time_t time_t}
                }
            }
            default {
                array set libs {
                    c /lib/libc.so
                    m /lib/libm.so
                    gdbm libgdbm.so
                    gmp libgmp.so
                    mathswig libmathswig0.5.so
                }
                array set types {
                    size_t int
                    clock_t long
                    time_t long
                    timeval {time_t time_t}
                }
            }
	    }
	}
	WINDOWS {
        #
        # found libraries
        # this array is used by the ::ffidl::find-lib
        # abstraction to store the resolved lib paths
        #
        # CHANGE - put your resolved lib paths here
        #
        array set libs {
            c msvcrt.dll
            m msvcrt.dll
            gdbm {}
            gmp gmp202.dll
            mathswig mathswig05.dll
        }
        #
        # found types
        # these arrays are used by the ::ffidl::find-type
        # abstraction to store resolved system types
        # and whether they have already been defined
        # with ::ffidl::typedef
        #
        # CHANGE - put your resolved system types here
        #
        array set types {
            size_t int
            clock_t long
            time_t long
            timeval {time_t time_t}
        }
        array set typedefs {
        }
	}
    }
    
    puts types=[array get types]
    puts libs=[array get libs]
    puts paths=$paths
}

#
# get the address of the string rep of a Tcl_Obj
# get the address of the unicode rep of a Tcl_Obj
# get the address of the bytearray rep of a Tcl_Obj
#
# CAUTION - anything which alters the Tcl_Obj may
# invalidate the results of this function.  Use
# only in circumstances where the Tcl_Obj will not
# be modified in any way.
#
# CAUTION - the memory pointed to by the addresses
# returned by ::ffidl::get-string and ::ffidl::get-unicode
# is managed by Tcl, the contents should never be
# modified.
#
# The memory pointed to by ::ffidl::get-bytearray may
# be modified if care is taken to respect its size,
# and if shared references to the bytearray object
# are known to be compatible with the modification.
#

#::ffidl::callout ::ffidl::get-string {pointer-obj} pointer [::ffidl::stubsymbol tcl stubs 340]; #Tcl_GetString
#::ffidl::callout ::ffidl::get-unicode {pointer-obj} pointer [::ffidl::stubsymbol tcl stubs 382]; #Tcl_GetUnicode
#::ffidl::callout ::ffidl::get-bytearray-from-obj {pointer-obj pointer-var} pointer [::ffidl::stubsymbol tcl stubs 33]; #Tcl_GetByteArrayFromObj

#proc ::ffidl::get-bytearray {obj} {
#    set len [binary format [::ffidl::info format int] 0]
#    ::ffidl::get-bytearray-from-obj $obj len
#}

#
# create a new string Tcl_Obj
# create a new unicode Tcl_Obj
# create a new bytearray Tcl_Obj
#
# I'm not sure if these are actually useful
#

#::ffidl::callout ::ffidl::new-string {pointer int} pointer-obj [::ffidl::stubsymbol tcl stubs 56]; #Tcl_NewStringObj
#::ffidl::callout ::ffidl::new-unicode {pointer int} pointer-obj [::ffidl::stubsymbol tcl stubs 378]; #Tcl_NewUnicodeObj
#::ffidl::callout ::ffidl::new-bytearray {pointer int} pointer-obj [::ffidl::stubsymbol tcl stubs 50]; #Tcl_NewByteArrayObj

#
# access the standard allocator, malloc, free, realloc
#
::ffidl::find-type size_t
set libc [::ffidl::find-lib c]
puts libc=$libc
::ffidl::callout ::ffidl::malloc {size_t} pointer [::ffidl::symbol $libc malloc]
::ffidl::callout ::ffidl::realloc {pointer size_t} pointer [::ffidl::symbol $libc realloc]
::ffidl::callout ::ffidl::free {pointer} void [::ffidl::symbol $libc free]

#
# Copy some memory at some location into a Tcl bytearray.
#
# Needless to say, this can be very hazardous to your
# program's health if things aren't sized correctly.
#

::ffidl::callout ::ffidl::memcpy {pointer-var pointer int} pointer [::ffidl::symbol $libc memcpy]

proc ::ffidl::peek {address nbytes} {
    set dst [binary format x$nbytes]
    ::ffidl::memcpy dst $address $nbytes
    set dst
}

#
# convert raw pointers, as integers, into Tcl_Obj's
#
#::ffidl::callout ::ffidl::pointer-into-string {pointer} pointer-utf8 [::ffidl::symbol [::ffidl::find-lib ffidl] ffidl_pointer_pun]
#::ffidl::callout ::ffidl::pointer-into-unicode {pointer} pointer-utf16 [::ffidl::symbol [::ffidl::find-lib ffidl] ffidl_pointer_pun]
#proc ::ffidl::pointer-into-bytearray {pointer length} {
    #set bytes [binary format x$length]
    #::ffidl::memcpy [::ffidl::get-bytearray $bytes] $pointer $length
    #set bytes
#}
