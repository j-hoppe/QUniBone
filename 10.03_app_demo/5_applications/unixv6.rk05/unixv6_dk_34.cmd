# inputfile for demo to select a rk05 device in the "device test" menu.
# Read in with command line option  "demo --cmdfile ..."
# mounts 3 "Unixv6" RK05 images
d			# device test menu

# en dl11		# use emulated serial console
# en kw11

pwr
.wait 3000		# wait for PDP-11 to reset
m i			# install max UNIBUS memory

# Deposit bootloader into memory
m ll dk.lst

en rk			# enable RK11 controller

en rk0			# enable drive #0
sd rk0			# select
p image v6bin.rk

en rk1			# enable drive #1
sd rk1			# select
p image v6doc.rk

en rk2			# enable drive #2
sd rk2			# select
p image v6src.rk

.print Disk drive now on track after 5 secs
.wait	6000		# wait until drive spins up
p                       # show all params of RL1


.print RK drives ready.
.print RK11 boot loader installed.
.print Start 10000 to boot from drive 0
.print Reload boot loader with "m ll"
.print Set terminal to 9600 7O1
.print On @ prompt, select kernel to run: "rkunix"


