***pi-bootloader***

The pi-bootloader is a multiboot-light loader for the Raspberry Pi.  This bootloader is different because it has a hardware and a server component.

**The hardware component**

This component is intended to be installed on the Pi, taking the place of `kernel.img` for the original RPi, or `kernel7.img` on the RPi2.  It will be loaded to the normal location (`0x8000`).  This component will then initialize the PL011 UART and receive data from the server, loading that into memory starting at `0x100000` as would a multiboot compliant loader.

**The server component**

This component will run on the development PC.  It will be fed a `cfg-file` file, which will contain the location of the kernel and other modules.  The bss of the kernel will be allocated and copied over the serial line as `0` bytes.  Then the modules will be padded to the next 4096 bytes and then copied to the serial port connected to the RPi in the order presented in the `cfg-file` file.  

At the same time, the server component will build the Multiboot Information structure, which `pi-bootloader` will pass to the kernel.  This structure will be copied to the RPi hardware in the end and will be copied to a location in lower memory.

**Limitations**

This is not a fully multiboot compliant loader.  Not even close.  There are some things to be aware of:
* The multiboot header is not checked.  No signature is checked and no flags are considered.  No matter what you ask for, you will only get module and memory information.
* The kernel ELF R/E Program Header section must end 4K aligned.  This is not checked.
* The kernel ELF RW Program Header section is assumed to end 4K aligned (though not a hard requirement).  This is not checked.
* The kernel ELF must be linked to load at address `0x100000`.  This is not checked.
* Parameters for the kernel or modules are not supported.  Module names will be the file name.

Additionally, be aware of the following:
* Currently, only RPi2 is supported.

**Cross Compiler**

I use crosstool-NG to create my cross compiler.  I used the `armv7-rpi2-linux-gnueabihf` compiler to build the hardware component.  The only modifications I made to the build config were to disable `gdb`, `automake`, and `autoconf`.  I also adapted the target location of the build to suite my own development environment.  See the documentation (https://crosstool-ng.github.io/docs/) for crosstool-NG for more information.  

I have to call out that this by default will build the C library and all the associated include files -- be cautious about what you include as there is not a functioning OS for most of these standard calls.

**Build System**

I typically use a mix of `make` and `tup` as my build system.  I use `tup` to actually perform the build and `make` to take care of all the additional (more complicated) things that I might want to execute.  By default, `make` will merely call `tup`.

You can get `tup` from http://gittup.org/tup/.  Once you have `tup` built and in the `$PATH`, you simply run `tup init` from the root of this project before you `make`.

**My Journal**

As with all my projects, I will be keeping a JOURNAL.md of my progress and my trials and to help document the problems and decisions I encounter.  There are a few reasons I keep this journal with each project (as opposed to one journal for everything or putting the comments in the code):
1. I sometimes need to revisit my own thinking and refer back to the journal.
1. If I put this level of documentation into the code, there would be more comments than code and the actual code would get lost.
1. I want to be able to keep track of times when I change my mind and the source needs to represent current state, but a bunch of bad decisions.
1. I want to be able to present this journal for others to learn from.

**Inspiration**

I want to call out and thank the following 2 projects for providing me the inspiration for this project.  Both projects have features (if not code) that they lend to this project.  I will, of course, call out when I borrow code from either project.

* https://github.com/mrvn/raspbootin
* https://github.com/jncronin/rpi-boot


