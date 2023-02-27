d			# device test menu

#pwr
 .wait 3000		# wait for PDP-11 to reset
m i			# install max UNIBUS memory

en ke                   # enable KE11-A EAE
en rf                   # enable RF11 fixed-head disk controller
en rs0                  # enable RS11 disk
sd rs0
p image rs0.dsk         # mount image
 
en rk			# enable RK11 controller
en rk0			# enable drive #0
sd rk0			# select
p image rk0.dsk 	# mount image

# poke bootstrap into memory
D 73700 012700
D 73702 177472
D 73704 012740
D 73706 000003
D 73710 012740
D 73712 140000
D 73714 012740
D 73716 054000
D 73720 012740
D 73722 176000
D 73724 012740
D 73726 000005
D 73730 105710
D 73732 002376
D 73734 000137
D 73736 054000
D 73740 012700
D 73742 177350
D 73744 005040
D 73746 010040
D 73750 012740
D 73752 000003
D 73754 105710
D 73756 002376
D 73760 005737
D 73762 177350
D 73764 001377
D 73766 112710
D 73770 000005
D 73772 105710
D 73774 002376
D 73776 005007

.print RF11 bootstrap is in memory at 73700.
.print Load address 73700 into PDP-11 via front panel,
.print Then set the Switch Register to 173700 and start the processor.
.print login as "root"

