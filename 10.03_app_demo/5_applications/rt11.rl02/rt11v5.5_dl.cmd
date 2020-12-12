# inputfile for demo to select a rl1 device in the "device test" menu.
# Read in with command line option  "demo --cmdfile ..."
d			# device menu

# en dl11		# use emulated serial console
# en kw11

pwr			# reboot PDP-11
.wait 3000		# wait for PDP-11 to reset
m i			# install max UNIBUS memory

# Deposit bootloader into memory
m ll dl.lst

en rl			# enable RL11 controller

# mount RT11 v5.5 in RL02 #0 and start
en rl0			# enable drive #0
sd rl0			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
# set type to "rl02"
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image rt11v5.5.rl02 # mount image file with test pattern
p runstopbutton 1	# press RUN/STOP, will start

# mount RT11 GAMES in RL02 #1 and start
en rl1			# enable drive #1
sd rl1			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
# set type to "rl02"
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image rt11v5.5_games.rl02	# mount image file with test pattern
p runstopbutton 1	# press RUN/STOP, will start

# mount scratch2 in RL02 #2 and start
en rl2			# enable drive #2
sd rl2			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
# set type to "rl02"
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image scratch2.rl02 	# mount image file with test pattern
p runstopbutton 1	# press RUN/STOP, will start

# mount scratch3 in RL02 #3 and start
en rl3			# enable drive #3
sd rl3			# select
p emulation_speed 10	# 10x speed. Load disk in 5 seconds
# set type to "rl02"
p runstopbutton 0	# released: "LOAD"
p powerswitch 1		# power on, now in "load" state
p image scratch3.rl02 	# mount image file with test pattern
p runstopbutton 1	# press RUN/STOP, will start

.print Disk drive now on track after 5 secs
.wait	6000		# wait until drive spins up
p                       # show all params of RL1

.print RL drives ready.
.print RL11 boot loader installed.
.print Start 10000 to boot from drive 0, 10010 for drive 1, ...
.print Reload with "m ll"



