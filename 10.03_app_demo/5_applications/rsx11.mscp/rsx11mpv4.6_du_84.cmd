# inputfile for demo to select a rsx11 device in the "device test" menu.
# Read in with command line option  "demo --cmdfile ..."
d			# device menu
pwr			# reboot PDP-11
.wait 3000		# wait for PDP-11 to reset
#m i			# install max UNIBUS memory

# Deposit bootloader into memory
m ll du.lst

en uda

# mount RSX11MP v4.6 in drive #0 and start
en uda0			# enable drive #0
sd uda0			# select drive #0

# set type to "RA80"
p type RA80
p uis 1
p image rsx11mpv4.6_du0_84.dsk # mount image file with test pattern
p

# Decus data disk in uda1:
en uda1			#enable drive #1
sd uda1			# select drive #1
p type RA80
p uis 1
p image rsx11mpv4.6_du1_84.dsk
p

en rl			# enable RL11 controller

# mount RSX v4.16 in RL02 #0 and start
en rl0			# enable drive #0
sd rl0			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
p type rl02
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image dl0.dsk  # mount image
#p image rsx11mp46.rl02  # mount image
p runstopbutton 0	# press RUN/STOP, will start

# mount DL1 in RL02 #1 and start
en rl1			# enable drive #1
sd rl1			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
p type rl02
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image dl1.dsk         # mount image
p runstopbutton 0	# press RUN/STOP, will start
# mount DL1 in RL02 #1 and start

en rl2			# enable drive #2
sd rl2			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
p type rl02
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image dl2.dsk         # mount image
p runstopbutton 0	# press RUN/STOP, will start
# mount DL2 in RL02 #2 and start

en rl3			# enable drive #3
sd rl3			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
p type rl02
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image dl3.dsk         # mount image
p runstopbutton 0	# press RUN/STOP, will start

# set up DL11 serial port
#sd dl11			# select
#p addr 760010		# csr = 160010
#p iv 310		# vec = 310
#en dl11			# enable DL11

.print MSCP drives ready.
.print UDA50 boot loader installed.
.print Start 10000 to boot from drive 0, 10010 for drive 1, ...
.print Reload with "m ll"
.print RSX11 is "PidP11" image with Bilquist TCP/IP.
.print EDT [1,2]STARTUP.CMD to adapt serial ports, etc.
.print Accounts: SYSTEM/SYSTEM, MATLOCK/VARMIT ?




