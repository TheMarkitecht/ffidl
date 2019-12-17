#
# atol for long return check
#
load ../FfidlJim.0.8b0.so
source ../ffidlrt.tcl

#
# the plain interfaces
#
ffidl-proc atol {pointer-utf8} long [ffidl-symbol [ffidl-find-lib c] atol]
ffidl-proc _strtol {pointer-utf8 pointer-var int} long [ffidl-symbol [ffidl-find-lib c] strtol]
ffidl-proc _strtoul {pointer-utf8 pointer-var int} {unsigned long} [ffidl-symbol [ffidl-find-lib c] strtoul]
#
# some cooked interfaces
#
proc strtol {str radix} {
    set endptr [binary format [ffidl-info format pointer] 0]
    set l [_strtol $str endptr $radix]
    binary scan $endptr [ffidl-info format pointer] endptr
    list $l $endptr
}
proc strtoul {str radix} {
    set endptr [binary format [ffidl-info format pointer] 0]
    set l [_strtoul $str endptr $radix]
    binary scan $endptr [ffidl-info format pointer] endptr
    list $l $endptr
}

puts 5=[strtol 5]
puts 999999=[strtol 999999]
