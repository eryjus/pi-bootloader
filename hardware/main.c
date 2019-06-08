//===================================================================================================================
//
//  main.c -- this is the main entry and executabe for the hardware component
//
//          Copyright (c)  2018 -- Adam Clark
//          Licensed under the BEER-WARE License, rev42 (see LICENSE.md)
//
// ------------------------------------------------------------------------------------------------------------------
//
//     Date      Tracker  Version  Pgmr  Description
//  -----------  -------  -------  ----  ---------------------------------------------------------------------------
//  2018-Dec-25  Initial   0.0.1   ADCL  Initial version
//  2019-Jun-08  Initial   0.0.1   ADCL  Sent the additional processors to the kernel code as well
//
//===================================================================================================================


#include <stdint.h>
#include <stdbool.h>

#define PL011 0

//
// -- These are some macros to help us with coding
//    --------------------------------------------
#define GET32(a)    (*((volatile uint32_t *)a))
#define PUT32(a,v)  (*((volatile uint32_t *)a) = v)

#define HWBASE      (0x3f000000)

#define GPIO_BASE   (HWBASE+0x200000)  
#define GPIO_FSEL1          (GPIO_BASE+0x04)            // GPIO Function Select 1
#define GPIO_GPPUD          (GPIO_BASE+0x94)            // GPIO Pin Pull Up/Down Enable
#define GPIO_GPPUDCLK1      (GPIO_BASE+0x98)            // GPIO Pin Pull Up/Down Enable Clock 0


#define AUX_BASE    (HWBASE+0x215000)                    
#define AUX_ENABLES         (AUX_BASE+0x004)            // Auxiliary Enables
#define AUX_MU_IO_REG       (AUX_BASE+0x040)            // Mini UART I/O Data
#define AUX_MU_IER_REG      (AUX_BASE+0x044)            // Mini UART Interrupt Enable
#define AUX_MU_IIR_REG      (AUX_BASE+0x048)            // Mini UART Interrupt Identify
#define AUX_MU_LCR_REG      (AUX_BASE+0x04c)            // Mini UART Line Control
#define AUX_MU_MCR_REG      (AUX_BASE+0x050)            // Mini UART Modem Control
#define AUX_MU_LSR_REG      (AUX_BASE+0x054)            // Mini UART Line Status
#define AUX_MU_CNTL_REG     (AUX_BASE+0x060)            // Mini UART Extra Control
#define AUX_MU_BAUD_REG     (AUX_BASE+0x068)            // Mini UART Baudrate


//
// -- These are prototypes for things outside this source file
//    --------------------------------------------------------
extern void DoNothing(void);
extern uint32_t GetCBAR(void);
extern void Halt(void);

void SerialPutChar(char c);


//
// -- These are some global variables
//    -------------------------------
const uint32_t hwLocn = 0x3f000000;


//
// -- Burn CPU cycles in an attempt to wait -- definitely not scientific!
//    -------------------------------------------------------------------
void BusyWait(uint32_t count) 
{
    while (count) {
        DoNothing();
        --count;
    }
}


//
// -- Initialize the serial port and get it ready to send and receive
//    ---------------------------------------------------------------
void SerialInit(void)
{
    // -- must start by enabling the mini-UART; no register access will work until...
    PUT32(AUX_ENABLES, 1);

    // -- Disable all interrupts
    PUT32(AUX_MU_IER_REG, 0);

    // -- Reset the control register
    PUT32(AUX_MU_CNTL_REG, 0);

    // -- Program the Line Control Register -- 8 bits, please
    PUT32(AUX_MU_LCR_REG, 3);

    // -- Program the Modem Control Register -- reset
    PUT32(AUX_MU_MCR_REG, 0);

    // -- Disable all interrupts -- again
    PUT32(AUX_MU_IER_REG, 0);

    // -- Clear all interrupts
    PUT32(AUX_MU_IIR_REG, 0xc6);

    // -- Set the BAUD to 115200 -- ((250,000,000/115200)/8)-1 = 270
    PUT32(AUX_MU_BAUD_REG, 270);

    // -- Select alternate function 5 to work on GPIO pin 14
    uint32_t sel = GET32(GPIO_FSEL1);
    sel &= ~(7<<12);
    sel |= (0b010<<12);
    sel &= ~(7<<15);
    sel |= (0b010<<15);
    PUT32(GPIO_FSEL1, sel);

    // -- Enable GPIO pins 14/15 only
    PUT32(GPIO_GPPUD, 0x00000000);
    BusyWait(150);
    PUT32(GPIO_GPPUDCLK1, (1<<14)|(1<<15));
    BusyWait(150);
    PUT32(GPIO_GPPUDCLK1, 0x00000000);              // LEARN: Why does this make sense?

    // -- Enable TX/RX
    PUT32(AUX_MU_CNTL_REG, 3);

    // -- clear the input buffer
    while ((GET32(AUX_MU_LSR_REG) & (1<<0)) != 0) GET32(AUX_MU_IO_REG);
}


//
// -- Put a character to the serial line -- note this works because for this we are only sending ASCII chars
//    ------------------------------------------------------------------------------------------------------
void SerialPutChar(char c)
{
    if (c == '\n') SerialPutChar('\r');
    while ((GET32(AUX_MU_LSR_REG) & (1<<5)) == 0) { }
    PUT32(AUX_MU_IO_REG, c);
}


//
// -- Put a string to the serial port
//    -------------------------------
void SerialPutS(const char *s)
{
    while (*s) {
        SerialPutChar(*s++);
    }
}


//
// -- Get a byte from the serial port -- note: not characters since we read binary values
//    -----------------------------------------------------------------------------------
uint8_t SerialGetByte(void)
{
    while ((GET32(AUX_MU_LSR_REG) & (1<<0)) == 0) { }
    return (uint8_t)(GET32(AUX_MU_IO_REG) & 0xff);
}


//
// -- These are used to sent the APs to the kernel as well
//    ----------------------------------------------------
const uint32_t mbiLoc = 0xfe000;
extern uint32_t entryPoint;


//
// -- This is the main entry point for the hardware component.  It will walk through the steps required to get
//    the kernel and related modules from the server, and then boot the OS.
//    --------------------------------------------------------------------------------------------------------
void kMain(uint32_t atags)
{
    typedef void (*kernel_t)(uint32_t r0, uint32_t r1, uint32_t r2) __attribute__((noreturn));
    kernel_t kernel = (kernel_t)0;
    const uint32_t kernelLoc = 0x100000;

    SerialInit();

    // -- this greeting should be sent to the screen on the server side -- then start the conversation.
    SerialPutS("\n'pi-bootloader' (hardware component) is loaded\n   Waiting for kernel and modules...\n");
    SerialPutS("\x03\x03\x03");     // send 3 breaks to the server to indicate that we are waiting for a kernel

    // -- get the size of the binaries (all-in) -- this is sent in little endian order
    uint32_t binSize = 0;
    char *sz = (char *)&binSize;

    sz[0] = SerialGetByte();
    sz[1] = SerialGetByte();
    sz[2] = SerialGetByte();
    sz[3] = SerialGetByte();
    
    uint8_t *mem = (uint8_t *)kernelLoc;

    // -- Good so far, get the bytes and store them at 0x100000
    SerialPutChar('\x06');
    while (binSize--) *mem++ = SerialGetByte();
    SerialPutChar('\x06');

    // -- Now duplicate the process for the mbi structure
    sz[0] = SerialGetByte();
    sz[1] = SerialGetByte();
    sz[2] = SerialGetByte();
    sz[3] = SerialGetByte();
    
    mem = (uint8_t *)mbiLoc;

    // -- Good so far, get the bytes and store them at 0x0fe000
    SerialPutChar('\x06');
    while (binSize--) *mem++ = SerialGetByte();
    SerialPutChar('\x06');

    // -- Get the Entry point
    uint32_t entry;
    char *e = (char *)&entry;
    e[0] = SerialGetByte();
    e[1] = SerialGetByte();
    e[2] = SerialGetByte();
    e[3] = SerialGetByte();
    SerialPutChar('\x06');

    // -- If we made it here without an error notify we are booting
    SerialPutS("Booting...\n");

    entryPoint = entry;
    kernel = (kernel_t)entry;
    __asm__ volatile("dsb");        // -- perform a memory synchronization since entry needs to be updated
    __asm__ volatile("sev");        // -- send an event tot he other cpus, signaling that it's time to go
    kernel(0x2badb002, mbiLoc, 0);
}