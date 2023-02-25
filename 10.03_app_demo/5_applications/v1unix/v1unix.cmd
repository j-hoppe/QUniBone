# inputfile for demo to select a rl1 device in the "device test" menu.
# Read in with command line option  "demo --cmdfile ..."
d			# device test menu

#pwr
# .wait 3000		# wait for PDP-11 to reset
m i			# install max UNIBUS memory

en ke
en rf
en rf0
sd rf0
p image rf0.dsk
 
en rk			# enable RL11 controller
en rk0			# enable drive #0
sd rk0			# select
p image rk0.dsk 	# mount image file with test pattern
