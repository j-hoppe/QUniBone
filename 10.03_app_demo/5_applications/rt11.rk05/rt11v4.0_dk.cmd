# inputfile for demo to select a rl1 device in the "device test" menu.
# Read in with command line option  "demo --cmdfile ..."
d			# device menu

# en dl11		# use emulated serial console
# en kw11

pwr			# reboot PDP-11
.wait 3000		# wait for PDP-11 to reset
m i			# install max  memory

# Deposit bootloader into memory
m ll dk.lst

en rk			# enable RK11 controller

en rk0			# enable drive #0
sd rk0			# select
p image RTRKV4.00 # www.classiccmp.org/PDP-11/RT-11/dists

en rk1			# enable drive #1
sd rk1			# select
p image scratch1.rk


.print Disk drive now on track after 3 secs
.wait	3000		# wait until drive spins up
p                       # show all params of RK1

.print RK drives ready.
.print RK11 boot loader installed.
.print Start 10000 to boot from drive 0, 10010 for drive 1, ...
.print Reload with "m ll"



