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
# long atol(const char *str);
ffidl::callout atol {pointer-utf8} long [ffidl::symbol $libc atol]
# long int strtol(const char *nptr, char **endptr, int base);
ffidl::callout _strtol {pointer-utf8 pointer-var int} long [ffidl::symbol $libc strtol]
# unsigned long int strtoul(const char *nptr, char **endptr, int base);
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

puts "commands=[join [lsort [info commands]] {  ~~  }]"
puts 79=[atol {79 junk}]
puts 5=[strtol {5 junk} 10]
puts 999999=[strtol {999999 junk} 10]
