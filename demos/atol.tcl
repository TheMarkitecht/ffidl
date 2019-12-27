package require binary

#
# atol for long return check
#
load ../FfidlJim.0.8b0.so
source ../ffidlrt.tcl

#
# the plain interfaces
#
set libc [::ffidl::find-lib c]
ffidl::callout atol {pointer-utf8} long [ffidl::symbol $libc atol]
ffidl::callout _strtol {pointer-utf8 pointer-var int} long [ffidl::symbol $libc strtol]
ffidl::callout _strtoul {pointer-utf8 pointer-var int} {unsigned long} [ffidl::symbol $libc strtoul]
#
# some cooked interfaces
#
# these should be ported from [binary] to Jim's [pack] package, for reliability, speed, and simplicity.  revisit.
proc strtol {str radix} {
    set endptr [binary format [ffidl::info format pointer] 0]
    set l [_strtol $str endptr $radix]
    binary scan $endptr [ffidl::info format pointer] endptr
    list $l $endptr
}
proc strtoul {str radix} {
    set endptr [binary format [ffidl::info format pointer] 0]
    set l [_strtoul $str endptr $radix]
    binary scan $endptr [ffidl::info format pointer] endptr
    list $l $endptr
}

puts 5=[strtol 5 10]
puts 999999=[strtol 999999 10]
