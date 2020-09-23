# full memory emulation
m
pwr
.wait 3000	# wait for PDP-11 to reset through ACLO/DCLO
m         	# emulate full memory
.wait 1000
lp zkma.ptap    # load into memory


.print *************************************************************
.print Now start ZKMA on PDP-11.
.print On M9312: "L 200" , "S"
.print ZKMA should only test 0-157776
.print

.input

.print Now starting UniBone DMA test in parallel with ZKMA on upper mem.
.print *************************************************************
tr 200000 757776

