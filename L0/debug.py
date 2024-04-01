import gdb
import os

# Register the quit hook
def on_quit():
    gdb.execute('kill')

gdb.events.exited.connect(on_quit)

gdb.execute("layout asm")

# Connect to the remote target
gdb.execute('target remote localhost:1234')

# Load the debug symbols
am_home = os.environ['AM_HOME']
path = f'{am_home}/am/src/x86/qemu/boot/boot.o'
gdb.execute(f'file {path}')

# This is the 0x7c00
gdb.Breakpoint('_start')

# This is where 32-bit code starts
gdb.Breakpoint('start32')

# Continue execution
gdb.execute('continue')
