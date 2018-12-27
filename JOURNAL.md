***pi-bootloader***

This file is my journal for building this project.  I do not make modifications to the journal except to occasionally go back and clean up formatting concerns.  Even the typos will persist.  The first entry is on the top while the bottom entry is the more recent. 

As you read the journal from top to bottom, you may see things revisited and decisions changed as the needs arise and as I learn more about what I am trying to accomplish.  Please enjoy!


### 2018-Dec-25

Today I made the decision to start this project.  It comes out of my need to improve my development cycle for the Raspberry Pi (which I will refer to as rpi in this project).  I have come to the conclusion that qemu does not emulate the rpi well at all, and especially the rpi2.  So, up until a couple days ago, my development cycle looked like the following:
* Make a code change on my development system (a robust VMware guest with 8 cores anf 16GB memory)
* Build this change into a binary
* E-mail the binary to myself (nothing is shared between the development system and my other systems)
* On my laptop, save the binary to the file system
* Insert a microSD card into a reader in the USB port in the back of the laptop (requires me to stand up to reach it)
* Mount the proper partition
* Copy the binary to the microSD card, replacing `kernel7.img`
* Umount the microSD partition
* Umount the other automatically mounted partitions
* Retrieve the microSD from the USB reader (again requires me to stand up to reach it)
* Insert the microSD into the rpi
* Plug in the microSD and execute the test, noting the results

Well, something had to give.  My laptop is woefully underpowered compared to the VMware development system and I am going to lose time in the compiles (which I use to to check my syntax and semantics regularly), but I should save more time in being able to compile and then plug the USB cable back into the computer and have the server serve the entire boot package to the rpi.

By the way, I use `tup` to take care of the core of the build because I find it very fast.  It does have its downfalls as well -- as I need to maintain a 'source' file in the binary target.  Some will have a big concern with this.  I do not -- or more to the point, I have been able to accept it and not worry too much about it.  This is especially true since the build will be very close to a single layer directory structure.

The goal is to reduce this cycle to something like this (assuming that the server component is running and the hardware component is already installed on the microSD):
* Make a code change on my laptop
* Build this change into a binary
* Plug the serial/USB converter into the laptop
* Execute the test and observe the results

In Century-OS, I am keeping to one function per file (most of the time anyway).  In this project, I will break the functions up logically and group several related functions into a single file.  The key difference is that this is purpose-built for the rpi hardware and I will not need to port this to another architecture like Century-OS.

---

With that all out of the way, I want to lay out the order in which things will happen.  This will basically end up being a light-weight protocol for sending and receiving over the serial line.

The server component will start in listening mode.  It will know the location of the `grub.cfg` file.  The server will be listening for 3 breaks.

The hardware component will load on boot.  As soon as it initializes the serial port and completes any other initialization required, it will send 3 breaks to the server over the serial line.

Once the 3 breaks are received, the server will evaluate the files to be sent to the hardware.  This will include opening and parsing the elf header information for the kernel.  Sizes will be computed, and this will be the first thing sent to the hardware.

The hardware will evaluate the location of the all the data to be sent and will check to make sure it will fit.  This will almost 100% be the case, but it may not or have some trouble with the size for some reason.  If things are good, then the hardware will send the 3 characters 'ACK' back to the server, or the 3 characters 'NAK' if there is a problem.

Once the server receives the 'ACK' character string, it will start to send the kernel, bss-expanded and zeroed, and justified to the next 4096 bytes, followed by each module in turn (in raw form) and justified to 4096 bytes. 

The hardware component will place this data at `0x100000`.

The hardware will keep track of the number of bytes received (which will be exactly what the server said it was going to send), and then will send the 3 character string 'ACK' to the server.  No error checking will be completed (CRC or otherwise).

Once receiving the 'ACK' string, the server will then send over the number of bytes in the MultiBoot Information structure.

The hardware will check that this data will fit.  If it will, then the hardware will send 'ACK' back and 'NAK' if there is an issue.  The hardware will place the MBI structure at `0x100000 - sizeof(struct MBI)` -- justified down to align with 4096 bytes.  There should be enough room for the structure.

Upon receiving the 'ACK' character string, the server will send over the MBI information.

Once that is received, the hardware will send back a final 'ACK' character string indicating that it is ready to boot.

Upon receiving the final 'ACK' string, the server will prepare to enter terminal mode and receive debugging serial output from the kernel.  Just before changing modes, the server will send its final 'ACK' to the hardware.

After receiving the final 'ACK' string the hardware will boot.

---

What I have going for me is that the complexity of most of this work is on the server side, which will be a simple linux application, whereas the the hardware component will only need to communicate with the PL011 UART. 

I will start by coding out the hardware component.  But first, I will commit these few initial files.

---

### 2018-Dec-26

I hit the ground running today and managed to complete the coding of the hardware component.  There are some changes to the plan above, which frankly I expected.  These are:
1. I am using the ASCII `ACK` and `NAK` character `\x06` and `\x15` respectively rather than the character stings.
1. The server component will need to check periodically to make sure that a `NAK` has not been received and if it has drop into terminal mode to display errors.
1. The starting kernel location needs to be fed into the hardware component so that it knows where to jump.  This was added after the mbi structure.

Other than the above changes (which really are not significant in my opinion), the plan remains the same.  At this point, I will commit this code to have it on a public repo.

---



