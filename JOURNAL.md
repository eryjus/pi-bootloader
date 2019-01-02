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

So, now I need to get the server component written.  The good thing is that this is really a PC application -- no cross-compiler; no emulation.  I can (for the most part anyway) write and test the application right here.

I think one of the first things I need to do is decide on the parameters for the command line.  I think I am going to need 3: 
1. The path to the serial device to use
2. The path to the `grub.cfg` file
2. The path to the sysroot from which all the files are rooted in the `grub.cfg` file

Alternatively, I could make it a requirement that the server component be executed from the sysroot so that all references in `grub.cfg` are relative to that directory and drop the last parameter.  Or even make it optional.

The first thing will be to get this server into a terminal mode, reading the serial port properly so that I can observe the boot process on the hardware (and at least get the greeting and the triple-break signal to start sending).

Ok, so one of the things I changed was to develop my own config file.  This is for a couple of reasons:
1. I do not want to support the entire grub command structure.  Certainly not grub2!
1. I think it is better to be able to keep a separate grub.cfg file for the times when a complete boot on real hardware storage is relevant.  It is not good to have to recreate that with each test.

So, with this, `grub.cfg` is out!  I am now going to refer to is as the `cfg-file`.

---

Well, I ended up just using `raspbootin` as a base for the server component.  This will not end up being the final version, but it allows me to get started with debugging the hardware component (by the way, the first test did nothing -- about what I expected).

I did manage to find a couple of bugs, including one I had to retrofit back into the *Century-OS* kernel.  Additional debugging will need to take place tomorrow.

---

### 2018-Dec-27

I am fighting to make sure that the hardware component works.  I have checked my code against several sources and everything looks good.  My `uart01.bin` does output to the console, so I know that the everything is working -- but it also uses the mini-UART.  

So, I am going to copy the `raspbootin` version (`raspbootin`) into this project and retool it to compile with my own toolset.  The `raspbootin` version does use the real PL011 UART, so it should work out of the box and allow me to test a bit.  That's the plan anyway.

---

This did not work.  I am getting the same results as my own version.  So, the next step is to change to the mini-UART where I know I have a working version.  And delete the whole `raspboothw` bit.

---

I'm an idiot!  I wrote my entry point as the following:

```
@@
@@ -- We will put this section at the start of the resulting binary
@@    -------------------------------------------------------------
    .section    entry
```

However, my linker script is looking for the section named `.entry`...  with a leading period.  I'm going to switch back and try to get the UART to work.  So, the PL011 UART is still not working, but the mini-UART does. 

---

### 2018-Dec-28

I spent a good part of the evening last night thinking about my goals with this project and what might be the problem with the PL011 UART.  I have some thought to "discuss" in this journal.

For the PL011 UART, I am probably suffering from a default configuration setting that is trying to set up some hardware flow control.  Probably -- I do not know for sure and if I did I would fix it.  I am not getting garbage, so it cannot be a baud rate problem.  So, the only conclusion I can draw is that of a flow control problem.  I can probably continue to research and get tot the bottom of the problem.  But....

What are my goals?  Well, put simply, the key goal here is to improve my overall development cycle by being able to send my loader and kernel and other modules to the rpi without having to write a new microSD each time.  I need a faster development and testing cycle.  And I really want that quickly so I can get back to the real project here -- Century-OS.  This project is a distraction, not key deliverable for me -- it's only necessary because qemu does not properly emulate the rpi hardware.

So, with those 2 points in mind, I am going to switch back to the mini-UART and move on to getting the binaries dumped.

The first order of business here is going to be to update the main program to read the `cfg-file` information and send the size data to the pi.

---

So I have the code to read `cfg-file` and determine the number of bytes to send written (not sending the bytes yet).  I am getting a segmentation fault:

```
[adam@adamlt rpi2b]$ pbl-server /dev/ttyUSB0 boot/grub/cfg-file 
pi-bootloader v0.0.1
  Please report bugs at https://github.com/eryjus/pi-bootloader
  [Portions copyright (C) 2013 Goswin von Brederlow]
### Listening on /dev/ttyUSB0     

'pi-bootloader' (hardware component) is loaded
   Waiting for kernel and modules...
Segmentation fault (core dumped)
```

... and I have no clue where as I put in no debugging code.  On a hunch, I put the code into `Trim()` first and the problem is there.

I finally got the `Trim()` function debugged.  I even was able to port my elf loader from Century-OS loader.  So, now I am getting a byte count (not accurate yet since I need to align it to 4096 bytes for each component).  So, I need to put some debugging code in to observe the conversation.  But that will be tomorrow -- I'm exhausted tonight.

---

### 2018-Dec-29

This morning I am going to get some of the debugging code written into the server portion.  This code will help me track where I am at with the conversation.

---

OK, at this point, I am not getting the error messages back from the pi -- only the ACK and then the PC program crashes.  This has something to do with the `select()` function and this is actually a new function to me.  So, off to do some reading.

---

I have changed tactics a bit later in the day.  I am now re-writing the `raspbootin` code to be more of a state machine.  I am following this a bit better.  And I do have the `select()` problems worked out.  While I am copying blocks of code from `raspbootin`, I am also able to understand each line much better, so I feel like I can "own" this code as my own as well.  The acknowledgement to `raspbootin` will still remain as the code is very much inspired by (and portions straight-up copied from) Goswin's.

---

### 2018-Dec-30

The start of today's focus is going to be to get the `cfg-file` read, checking the sizes of the kernel and modules (up-adjusting them as needed) and then to get that size reported to the rpi.  I am able to get the `cfg-file` read into memory and I am breaking up on lines properly.  Now it needs to be parsed into the actual file names and checked for validity.

The `ConfigLine_t` structure needs to be built for each line so that I can have it ready send to the rpi.

---

I have gotten to the point where I am sending the size to the pi and it is reporting that it is too big -- at 379K.  This is clearly wrong, so I need to go look at the pi code to see what is going on there.  I also need to be able to provide some feedback via TTY.

---

### 2018-Dec-31

I have decided that I want to get this working for a simple elf first (no modules) -- just to make debugging a little easier.  I have a `uart01.elf` that should fit the bill and will use that to make sure I can get the elf loaded and sent properly and then get back into tty mode to receive the output.

There is quite a bit of debugging to do just to get this working....

---

I've been on `freenode#osdev` for quite a while today...  I have not gotten too much done.  However, what I do know is that I am not getting any data back from the RPi...  Or at least none that is being cached by the OS.  I am going to start to look for output from the RPi as I loop through sending the kernel.

This resulted in nothing coming back.  I know I can send data and receive data.  The only thing I can think of is that the byte count is getting messed up and therefore the loop is never ending.  Since my test is only 4096 bytes, I can hard code that to make sure I hit that limit and see if I can get an ACK back.  That failed as well -- I never got a response back.

---

### 2019-Jan-01

Happy New Year!

Well, I am going to start the day taking a step back and working on getting just *something* to boot.  In this case, it will be `uart01.img` that is refactored to run at address `0x100000`.  I need the most trivial case to work before I start getting too fancy.  So, this will be the revised version:
* Server will boot and wait for a connection (issue a greeting)
* Hardware will boot and issue a greeting (server to display)
* Hardware will send 3 X 0x03 characters
* Server will send the binary size, which the Hardware will acknowledge
* Server will send the image (and the hardware will place it at `0x100000`)
* Server will return to tty mode
* Hardware will inform it is booting (which Server will print to the screen)
* Hardware will boot the image by jumping to address `0x100000`

This means I will be stripping out most of the code I have written so far.  It's good code I believe, so I will need to keep it -- just in a save location.

---

Ah-hah!  I've found something.  I got the Hardware to report back the size it is being given, and that size is not the same as what I am sending.

```
[adam@adamlt boot]$ pbl-server /dev/ttyUSB0 uart01.img 
Raspbootcom V1.0
### Listening on /dev/ttyUSB0     

'pi-bootloader' (hardware component) is loaded
   Waiting for kernel and modules...
### sending kernel uart01.img [292 byte]
### Size reported back as 74752 bytes
OK
### finished sending
```

In fact, 292 is `0x0124` and 74752 is `0x012400`.  So there is an extra byte being handed to the Hardware or back to the Server.  However, if it is being handed to the Hardware, then it would explain why the Hardware never boots.  I will focus my attention on the Server component and what is being sent first, but I might have to flush the buffer on startup for the Hardware.

It turns out I needed to flush the receiver buffer on the RPi.  Once I got that complete I am getting the result I am looking for:

```
### Listening on /dev/ttyUSB0     

'pi-bootloader' (hardware component) is loaded
   Waiting for kernel and modules...
### sending kernel uart01.img [292 byte; 292 size]
### Size reported back as 292 bytes
OK
### finished sending
Bo01234567012345670123456701234567012345670123456701234567012345670123456701234567012345670123456701234567012345670123456701234567012345670123456701
```

Well, mostly.  I think I need to put a short delay into the code to make sure I can get "Booting" off the UART before it is reset by the resulting kernel.  At the same time I am going to work on some code cleanup.

So, I have the Hardware component cleaned back up (with some code missing but that is OK).  The next step is to make sure I am getting the conversation properly handled on both sizes.  The Hardware is booting, but the Server is not expecting it to and fails back into tty mode.  All-in-all it is working; just not the way I want it to.  Oh, and by the way, it is properly sending the ELF version, not the image!  YAY!  So, with just a little bit of work, I should be able to get my real loader executing.  And it was only a single line of code to add.  This is the `pi-bootloader` running an ELF properly:

```
[adam@adamlt pi-bootloader]$ pbl-server /dev/ttyUSB0 uart01.cfg                                               
pi-bootloader v0.0.1
  (C) 2018 Adam Clark under the BEER-WARE license
  (Portions copyright (C) 2013 Goswin von Brederlow under GNUGPL v3)
  Please report bugs at https://github.com/eryjus/pi-bootloader
### Waiting for /dev/ttyUSB0...
### Listening on /dev/ttyUSB0     

'pi-bootloader' (hardware component) is loaded
   Waiting for kernel and modules...
Preparing to send uart01.cfg data
File 1 size is 69632
Notifying the RPi that 4096 bytes will be sent
Sending kernel (4096 bytes sent)...
Done
waiting for data
Booting...
012345670123456701234567012345670123456701234567012345670123
```

Now, the next thing is going to be to get Century-OS's `loader.elf` to load and execute.  This will provide me some information on what I will need to absolutely populate in the MBI structure.  I will shore the program up by arranging to send the MBI data and sending that.

Well, that test was not clean.  I got a resource closed:

```
Preparing to send century.cfg data
File 1 size is 131072
Notifying the RPi that 126976 bytes will be sent
write() to dev: Resource temporarily unavailable
```

The size is up-adjusted properly.  So, I need to try to figure out where the error is happening and why....  Which ended up being just a short bit of cleanup.

However the loader is not actually booting.  This is likely because the entry point is not the first byte in the file and that is where we are set up to jump.  This should be relatively easy to correct.

The loader is still not booting properly.  However, I checked the code on Century-OS and I am using the PL011 UART rather than the mini-UART.  I think that for me to get anything meaningful at this point, I need to change that code.  So, from here, I am going to switch to the Century-OS code and get that cleaned up.

I created a dump of the entry point of the loader.  This is what I got from that dump:

```
Sending the Entry point as 10000c
Waiting for the rpi to boot
0x1badb002
0x00000006
0xe4524ff8
0xee103fb0
0xe2033003
0xe3530000
0x0a00064f
0xe320f003
0xeafffffd
0xe92d4370
Booting...
```

The bits match the `loader.map`:

```
00100000 <_loaderStart>:
  100000:	1badb002 	blne	fec6c010 <ST_CLO+0xbfc6900c>
  100004:	00000006 	andeq	r0, r0, r6
  100008:	e4524ff8 	ldrb	r4, [r2], #-4088	; 0xfffff008

0010000c <_start>:
  10000c:	ee103fb0 	mrc	15, 0, r3, cr0, cr0, {5}
  100010:	e2033003 	and	r3, r3, #3
  100014:	e3530000 	cmp	r3, #0
  100018:	0a00064f 	beq	10195c <initialize>
```

But there is an initialization that jumps off to something much deeper into the code, so I want to check that location as well.

---

I added some code to see what the entry point I was sending is.  I got back:

```
Sending the Entry point as 10000c
Waiting for the rpi to boot
Entry confirm:
0x00000000

0x100000:
0x1badb002
```

This is interesting since it is likely that I am sending too many `0`s to fill in the bss properly.  I will need to go back to that code to make sure I am sending the data properly there.  And this is the problem:

```
Notifying the RPi that 126976 bytes will be sent
65536 of 65536 bytes were written
51917 of 51917 bytes were written
1331 of 1331 bytes were written
4 of 4 bytes were written
12284 of 12284 bytes were written
Sending kernel (131072 bytes sent)...
```

Cleaning the above output up a bit and using a calculator, I am now certain I am sending 4096 bytes more than I am supposed to.  The program headers say this:

```
Program Headers:
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x001000 0x00100000 0x00100000 0x1cacd 0x1cacd R E 0x1000
  LOAD           0x01e000 0x0011d000 0x0011d000 0x00004 0x0233c RW  0x1000
```

... such that the first section should be 117453 bytes or, rounding up, 118784 bytes.  The second section is supposed to be 9020 bytes or, rounding up, 12288 bytes.

Section 1 was reported to really send 118784 bytes.  Section 2 was reported to send 12288 bytes.  So these are correct.  And they add up to 131072 like they should.  So, it's the number of bytes in the beginning that is wrong.  And the problem ended up being this block of code that from `ParseElf()`:

```C
    for (int i = 0; i < elfSects; i ++) {
        byteCnt += phdr[i].p_memsz;
        if (byteCnt & 0xfff) byteCnt = (byteCnt & 0xfffff000) + 0x1000;
    }
```

The problem was the check of `byteCnt` was outside the loop; moving it inside the loop fixed the problem.  And now I am getting a fatal error since I am not handing off a memory map or any MBI information really.  But the kernel is booting!!

---

At this point, I have everything sent to the rpi except the additional modules.  This includes the MBI structure.  From here, I will have to make modifications to both Century-OS and pi-bootloader to get this debugged.  Right now, it makes the most sense to track my work here, even though it will require changes to Century-OS to add debugging code.  The point here is that there should not be any changed to the overall logic in Century-OS, and if there are I will be documenting them there.

