# inputfile for demo to select a rl0 device in the "device test" menu.
# Read in with command line option  "demo --cmdfile ..."
d			# device menu
pwr			# reboot PDP-11
.wait 3000		# wait for PDP-11 to reset
m i			# install max UNIBUS memory

# Deposit bootloader into memory
m ll dl.lst

en rl			# enable RL11 controller

# mount UNIXV6 in RL02 #0 and start
en rl0			# enable drive #0
sd rl0			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
# set type to "rl02"
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image unix_v6.rl02	# mount image file
p runstopbutton 1	# press RUN/STOP, will start

# mount scratch disk in RL02 #1 and start
en rl1			# enable drive #1
sd rl1			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
# set type to "rl02"
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image scratch01.rl02	# mount image file with test pattern
p runstopbutton 1	# press RUN/STOP, will start

.print Disk drive now on track after 5 secs
.wait	6000		# wait until drive spins up
p                       # show all params of RL1

.print RL drives ready.
.print RL11 boot loader installed.
.print Start 10000 to boot from drive 0, 10010 for drive 1, ...
.print Reload with "m ll"
.print This is UNIXV6 rescued by Tim Shoppa, see
.print     https://minnie.tuhs.org/Archive/Distributions/Research/Tim_Shoppa_v6/
.print Unix V6 bootloader shows a "!", enter "unix"
.print First command on the # prompt would be: "STTY -LCASE"



