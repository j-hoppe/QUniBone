# ~/.profile: executed by Bourne-compatible login shells.

if [ "$BASH" ]; then
  if [ -f ~/.bashrc ]; then
    . ~/.bashrc
  fi
fi

mesg n

# Derek Molloy,, page 223
# access to cape manager slots and pinmuxing
export SLOTS=/sys/devices/platform/bone_capemgr/slots
export PINS=/sys/kernel/debug/pinctrl/44e10800.pinmux/pins

# Autostart an UniBone device emulation
# - Execution only on one login session of physical UART.
# - Hangs due to missing CPU arbitration if not run
#   in a real PDP-11 system.

case `tty` in
# `tty` under Debian is like "/dev/ttyS1"
/dev/ttyS1)
        ./rt11.sh
;;
esac
