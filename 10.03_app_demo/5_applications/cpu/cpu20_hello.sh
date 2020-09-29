# starts PDP11/20 emulation
# and executes a "Hello world" on a physical DL11 card
# Main PDP-1120 must be HALTed
cd ~/10.03_app_demo/5_applications/cpu
~/10.03_app_demo/4_deploy/demo --verbose --cmdfile cpu20_hello.cmd $*

