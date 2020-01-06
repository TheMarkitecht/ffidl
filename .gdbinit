db -output /home/x/debug-dashboard
db source -output /home/x/debug-source

db -layout breaks threads stack source
db source -style context 20
db assembly -style context 5
db stack -style limit 8

file ../jimsh/jimsh
set args atol.tcl
set cwd demos
set solib-search-path .:..

b jim-load.c:33
r
b ffidl.c:3243
#b tcl_ffidl_callout
c


db
db

