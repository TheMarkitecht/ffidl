set code [read [open generic/ffidl.c r]]
set all [regexp -all -inline -expanded { Tcl_ \w+ \( } $code]
set all [lsort -unique $all]

set newCode $code
foreach match $all {
    set bare [string range $match 4 end-1]
    set full Jim_$bare\(
    set newCode [string map [list $match $full] $newCode]
}

puts [open generic/ffidl.c w] $newCode
