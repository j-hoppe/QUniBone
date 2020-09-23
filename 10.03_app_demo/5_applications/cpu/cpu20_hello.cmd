# Inputfile for demo to execute "Hello world"
# Uses emulated CPU and physical DL11
# Read in with command line option  "demo --cmdfile ..."

dc			# "device with cpu" menu

m i   			# emulate missing memory

en cpu20		# switch on emulated 11/20 CPU
sd cpu20		# select

m ll serial.lst		# load test program

p

init

.print Emulated PDP-11/20 CPU will now output "Hello world"
.print and enter a serial echo loop on physical DL11 at 177650.
.print Make sure physical CPU is disabled.

.input

p c 1

.print CPU20 started


