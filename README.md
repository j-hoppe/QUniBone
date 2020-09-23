# QUniBone
This is the software for both
Linux-to-UNIBUS bridge "UniBone"
and
Linux-to-QBUS bridge "QBone"

"UniBone" connects a BeagleBone Black micro Linux system to ancient DEC UNIBUS,
"QBone" does the same for DEC QBUS.

UniBone/QBone can keep old PDP-11s running, by emulating devices and aiding in repair.

As UNIBUS and QBUS are quite similar, only one software project compiles for both devices.

In-source differentiation is done via "#define UNIBUS" or "#define QBUS",
Source files special to only one bus are marked with suffix "_u" respective "_q".

The "QBone" part of project "QUniBone is under heavy development.

See [project page at retrocmp.com](http://retrocmp.com/projects/unibone/).
