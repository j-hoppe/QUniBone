
# DevicetreeOverlay:
# compile, install,reboot
# Then verify with
#       cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins
# Overlay name is always  "UniBone-00A0", as written into cape -EEPROM

export SOURCE=UniBone

dtc/dtc -O dtb -o $SOURCE-00B0.dtbo -b 0 -@ $SOURCE.dts
cp $SOURCE-00B0.dtbo /lib/firmware/UniBone-00B0.dtbo
reboot


