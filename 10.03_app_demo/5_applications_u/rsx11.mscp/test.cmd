# inputfile for demo to select a rl1 device in the "device test" menu.
# Read in with command line option  "demo --cmdfile ..."
d			# device test menu

# Use emulated DL11, for scripting
sd dl11
p p ttyS2		# use "UART2 connector
p rb 300		# "type" injected chars slowly with 300 baud
# en dl11
# en kw11			# RSX11M needs clock

pwr
.wait 3000		# wait for PDP-11 to reset
m i			# install max UNIBUS memory

# Deposit bootloader into memory
m ll du.lst

en uda			# enable UDA50 controller

# mount RSX in MSCP drive #0
en uda0			# enable drive #0
sd uda0			# select
p type RA70
p image rsx11m_4_8_bl70.dsk  # mount image
p useimagesize	1

# mount test disk in MSCP drive #1
en uda1			# enable drive #1
sd uda1			# select
p type RA70
p image rsxm70.dsk      # mount image
p useimagesize	1


en rl			# enable RL11 controller

# mount RSX v4.16 in RL02 #0 and start
en rl0			# enable drive #0
sd rl0			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
p type rl02
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image rsxm70.rl02  # mount image
p runstopbutton 1	# press RUN/STOP, will start

# mount DL1 in RL02 #1 and start
en rl1			# enable drive #1
sd rl1			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
p type rl02
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image rsxdl1.rl02         # mount image
p runstopbutton 1	# press RUN/STOP, will start



.print Disk drive now on track after 5 secs
.wait	5000		# wait until drive spins up

p                       # show all params

.print MSCP drives ready.
.print UDA50 boot loader installed.
.print Start 10000 to boot from drive 0, 10010 for drive 1, ...
.print Reload with "m ll"

.end

# boot RSX11 from DU
dl11 rcv L\x2010000\r
dl11 rcv 500 S\r
dl11 wait 20000 Please\x20enter\x20time\x20and\x20date
dl11 rcv 500 20:00 27-aug-2019\r
dl11 wait 10000 ENTER\x20LINE\x20WIDTH
dl11 rcv 1000 80\r
dl11 wait 10000 >@
# startup complete
# logout, login as SYSTEM/SYSTEM
dl11 rcv 1000 LOG\r
dl11 rcv 3000 HELLO\x20SYSTEM\r
dl11 rcv 3000 SYSTEM\r

