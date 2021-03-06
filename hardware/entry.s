@@===================================================================================================================
@@
@@  entry.s -- This will be the main entry point for the hardware component
@@
@@          Copyright (c)  2018 -- Adam Clark
@@          Licensed under the BEER-WARE License, rev42 (see LICENSE.md)
@@
@@  The RPi2 hardware boots into hyp mode by default.  We actually want svc mode, so we need to thunk the CPUs 
@@  into that mode.  This is similar to getting the x86 processor into user mode -- we have to make the CPU believe
@@  it was in that mode to begin with and pretend-return back to that mode.
@@
@@ ------------------------------------------------------------------------------------------------------------------
@@
@@     Date      Tracker  Version  Pgmr  Description
@@  -----------  -------  -------  ----  ---------------------------------------------------------------------------
@@  2018-Dec-25  Initial   0.0.1   ADCL  Initial version
@@  2019-Jun-08  Initial   0.0.1   ADCL  Send the APs to the kernel code as well
@@
@@===================================================================================================================


@@
@@ -- Expose some global addresses
@@    ----------------------------
    .globl      _start
    .globl      DoNothing
    .globl      GetCBAR
    .globl      Halt
    .globl      entryPoint


@@
@@ -- We will put this section at the start of the resulting binary
@@    -------------------------------------------------------------
    .section    .entry


@@
@@ -- This is the entry point.  The key here is that this must be the very first instruction to execute
@@    and must reside at 0x8000.
@@    -------------------------------------------------------------------------------------------------
_start:
    mrs     r0,cpsr                     @@ get the current program status register
    and     r0,#0x1f                    @@ and mask out the mode bits
    cmp     r0,#0x1a                    @@ are we in hyp mode?
    beq     hyp                         @@ if we are in hyp mode, go to that section
    cpsid   iaf,#0x13                   @@ if not switch to svc mode, ensure we have a stack for the kernel; no ints
    b       cont                        @@ and then jump to set up the stack

@@ -- from here we are in hyp mode so we need to exception return to the svc mode
hyp:
    mrs     r0,cpsr                     @@ get the cpsr again
    and     r0,#~0x1f                   @@ clear the mode bits
    orr     r0,#0x013                   @@ set the mode for svc
    orr     r0,#1<<6|1<<7|1<<8          @@ disable interrupts as well
    msr     spsr_cxsf,r0                @@ and save that in the spsr

    ldr     r0,=cont                    @@ get the address where we continue
    msr     elr_hyp,r0                  @@ store that in the elr register

    eret                                @@ this is an exception return

@@ -- everyone continues from here
cont:
    mov     sp,#0x8000                  @@ set the stack

    mrc     p15,0,r3,c0,c0,5            @@ Read Multiprocessor Affinity Register
    and     r3,r3,#0x3                  @@ Extract CPU ID bits
    cmp     r3,#0
    beq     initialize                  @@ if we’re on CPU0 goto the start

@@ -- all other cores will drop in to this loop - a low power mode loop waiting to be able to jump to the kernel
wait_loop:
    wfe                                 @@ wait for event

    ldr     r4,=entryPoint              @@ get the address of the kernel
    ldr     r4,[r4]                     @@ and the contents of that variable
    cmp     r4,#0                       @@ has the kernel been set yet?
    beq     wait_loop                   @@ if not, then we can loop and wait some more

    mov     r0,#0xb002                  @@ load the registers with the boot values
    movt    r0,#0x2bad
    ldr     r1,=mbiLoc                  @@ this is a variable address
    ldr     r1,[r1]                     @@ and this is its contents
    mov     r2,#0

    mov     pc,r4                       @@ jump to the kenrel code -- the other cpus do not have a valid stack

Halt:
    wfi
    b       Halt

@@ -- Clear out bss
initialize:
    ldr        r4,=_bssStart
    ldr        r9,=_bssEnd
    mov        r5,#0
    mov        r6,#0
    mov        r7,#0
    mov        r8,#0

bssLoop:
@@ -- store multiple at r4
    stmia    r4!, {r5-r8}

@@ -- If we're still below _bssEnd, loop
    cmp        r4,r9
    blo     bssLoop

@@ -- Finally jump to the main entry point
    mov     r0,r2                       @@ get the ATAGS and pass that to kMain()
    bl      kMain
    b       Halt


@@
@@ -- Get the hardware location from the CBAR
@@    ---------------------------------------
GetCBAR:
    mrc     p15,4,r0,c15,c0,0               @@ This gets the cp15 register 15 sub-reg 0
    mov     pc,lr                           @@ return


@@
@@ -- Do-nothing function call to burn cpu cycles
@@    -------------------------------------------
DoNothing:
    mov     pc,lr                           @@ just return, but the compiler cannot optimize


@@
@@ -- This is the entry point, stored from the elf loader
@@    ---------------------------------------------------
entryPoint:
    .word   0
