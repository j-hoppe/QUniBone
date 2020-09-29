# inputfile for demo to just emulate max memory
# Read in with command line option  "demo --cmdfile ..."
d			# device menu


pwr			# reboot PDP-11
.wait 3000		# wait for PDP-11 to reset
m i			# install max memory
