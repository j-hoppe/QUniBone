# inputfile for demo to select a rl1 device in the "device test" menu.
# Read in with command line option  "demo --cmdfile ..."

# Assume CPU stopped. No DMA until booted!
d			# device test menu

# first, make a serial port. Default ist
# sd dl11
# p p ttyS2		# use "UART2 connector
# en dl11
# en kw11


# CPU stopped, install M9312 + boot into console emulator
sd m9312		# further commands form M9312
p v 5			# verbosity "debug"
p bl DIAG		# set start label to console emulator entry 765020
p cer 23-248F1.lst	# plug console emulator ROM into socket
p br1 23-756A9.lst	# plug RK BOOT ROM into socket1
p br2 23-751A9.lst	# plug RL BOOT ROM into socket2
p br3 23-767A9.lst	# plug MSCP DU BOOT ROM into socket3
p
en m9312		# online

pwr			# reset, should boot into console emulator now

.print You should see console emulators "@" prompt now
.input Hit return to autoboot DL0 into XXDP

dis m9312
p bl dl0n		# set new boot address
en m9312

en rl			# enable RL11 controller

# mount XXDP25 in RL02 #0 and start
en rl0			# enable drive #0
sd rl0			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
# set type to "rl02"
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image xxdp25.rl02 	# mount image file with test pattern
#p runstopbutton 1	# press RUN/STOP, will start
p                       # show all params of RL1

pwr			# reboot
.wait 1000		# wait for PDP-11 to reset
.print DL bootloader waiting for drive to become READY.
m i			# install max UNIBUS memory, CPU running now

p runstopbutton 1	# press RUN/STOP, will start, then boot
.wait	5000		# wait until drive spins up
.print Disk drive now on track after 5 secs, XXDP boots
.print Execute diag ZM9BE0 to test M9312 emulation.
