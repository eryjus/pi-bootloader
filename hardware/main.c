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
//
//===================================================================================================================


#include <stdint.h>
#include <stdbool.h>


//
// -- These are some macros to help us with coding
//    --------------------------------------------
#define GET32(a)    (*(volatile uint32_t *)(a))
#define PUT32(a,v)  (*(volatile uint32_t *)(a)) = v

#define UART_BASE   (hwLocn + 0x201000)
#define UART_DR             (UART_BASE+0x00)                      // Data register
#define UART_RSRECR         (UART_BASE+0x04)                      // operation status and error clear register
#define UART_FR             (UART_BASE+0x10)                      // Flag Register
#define UART_IBRD           (UART_BASE+0x24)                      // Integer Baud Rate Divisor
#define UART_FBRD           (UART_BASE+0x28)                      // Fractional Baud Rate Divisor
#define UART_LCRH           (UART_BASE+0x2c)                      // Line Control Register
#define UART_CR             (UART_BASE+0x30)                      // Control Register
#define UART_IFLS           (UART_BASE+0x34)                      // interrupt FIFO Select register
#define UART_IMSC           (UART_BASE+0x38)                      // Interrupt Mask Clear Register
#define UART_RIS            (UART_BASE+0x3c)                      // Raw Interrupt Status Register
#define UART_MIS            (UART_BASE+0x40)                      // Masked Interrupt Status Register
#define UART_ICR            (UART_BASE+0x44)                      // Interrupt Clear Register
#define UART_DMACR          (UART_BASE+0x48)                      // DMA Control Register
#define UART_ITCR           (UART_BASE+0x80)                      // Test Control Register
#define UART_ITIP           (UART_BASE+0x84)                      // Integration Test Input Register
#define UART_ITOP           (UART_BASE+0x88)                      // Integration Test Output Register
#define UART_TDR            (UART_BASE+0x8c)                      // FIFO Test Data

#define GPIO_BASE   (hwLocn+0x200000)          
#define GPIO_GPPUD          (GPIO_BASE+0x94)            // GPIO Pin Pull Up/Down Enable
#define GPIO_GPPUDCLK1      (GPIO_BASE+0x98)            // GPIO Pin Pull Up/Down Enable Clock 0



//
// -- These are prototypes for things outside this source file
//    --------------------------------------------------------
extern void DoNothing(void);
extern uint32_t GetCBAR(void);
extern void Halt(void);


//
// -- These are some global variables
//    -------------------------------
uint32_t hwLocn;


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
// -- Get the hardware memory mapped IO locations from the config register
//    --------------------------------------------------------------------
void GetHardwareLocation(void)
{   
    hwLocn = GetCBAR();

    // -- In case we get nothing back, default to the rpi2 default location
    if (hwLocn == 0) hwLocn = 0x3f000000;
}


//
// -- Initialize the serial port and get it ready to send and receive
//    ---------------------------------------------------------------
void SerialInit(void)
{
    // -- Disable the UART
    PUT32(UART_CR, 0x00000000);

    // -- Enable GPIO pins 14/15 only
    PUT32(GPIO_GPPUD, 0x00000000);
    BusyWait(150);
    PUT32(GPIO_GPPUDCLK1, (1<<14) | (1<<15));
    BusyWait(150);
    PUT32(GPIO_GPPUDCLK1, 0x00000000);              // LEARN: Why does this make sense?

    // -- Clear any pending interrupts
    PUT32(UART_ICR, 0xffffffff);

    // -- disable interrupts
    PUT32(UART_IMSC, (1<<1) | (1<<4) | (1<<5) | (1<<6) | (1<<7) | (1<<8) | (1<<9) | (1<<10));

    // -- Enable the FIFO queues
    uint32_t lcrh = (1<<4);

    // -- Set the data width size
    lcrh |= ((0b11)<<5);

    // -- Finally, configure the UART
    PUT32(UART_IBRD, 1);
    PUT32(UART_FBRD, 40);
    PUT32(UART_LCRH, lcrh);

    // -- Enable the newly configured UART (not transmitting/receiving yet)
    PUT32(UART_CR, (1<<0) | (1<<8) | (1<<9));
}


//
// -- Put a character to the serial line -- note this works because for this we are only sending ASCII chars
//    ------------------------------------------------------------------------------------------------------
void SerialPutChar(char c)
{
    if (c == '\n') SerialPutChar('\r');
    while ((GET32(UART_FR) & (1<<5)) == 0) { }

    PUT32(UART_DR, c);
}


//
// -- Put a string to the serial port
//    -------------------------------
void SerialPutS(const char *s)
{
    while (*s) {
        SerialPutChar(*s);
        ++s;
    }
}


//
// -- Get a byte from the serial port -- note: not characters since we read binary values
//    -----------------------------------------------------------------------------------
uint8_t SerialGetByte(void)
{
    while ((GET32(UART_FR) & (1<<4)) == 0) { }

    return (uint8_t)(GET32(UART_DR) & 0xff);
}


//
// -- This is the main entry point for the hardware component.  It will walk through the steps required to get
//    the kernel and related modules from the server, and then boot the OS.
//    --------------------------------------------------------------------------------------------------------
void kMain(uint32_t atags)
{
    typedef void (*kernel_t)(uint32_t sig, uint32_t mbi, uint32_t atag) __attribute__((noreturn));
    kernel_t kernel;

    GetHardwareLocation();
    SerialInit();

    // -- this greeting should be sent to the screen on the server side -- then start the conversation.
    SerialPutS("\n'pi-bootloader' (hardware component) is booted\n   Waiting for kernel and modules...\n");
    SerialPutS("\x03\x03\x03");     // send 3 breaks to the server to indicate that we are waiting for a kernel

    // -- get the size of the binaries (all-in) -- this is sent in little endian order
    uint32_t binSize = 0;
    binSize = SerialGetByte();
    binSize |= (SerialGetByte() << 8);
    binSize |= (SerialGetByte() << 16);
    binSize |= (SerialGetByte() << 24);

    // -- we owe the server an ACK or NAK.  Only NAK if the binaries are too big
    if (binSize > (0x40000000 - 0x100000)) {
        SerialPutS("\x015\nBinary size too big\n");
        Halt();
    }

    uint8_t *mem = (uint8_t *)0x100000;
    uint32_t soFar = 0;
    bool error = false;

    // -- Good so far, get the bytes and store them at 0x100000
    SerialPutS("\x06");

    while (soFar < binSize) {
        *mem = SerialGetByte();
        ++mem;
        ++soFar;

        if (error) {
            // -- The server will check for a NAK between bytes
            SerialPutS("\x15\nError or timeout receiving binaries\n");
            Halt();
        }
    }

    // -- If we made it here without an error, indicate we are ready for the size of the MBI
    SerialPutS("\x06");

    // -- get the size of the binaries (all-in) -- this is sent in little endian order
    uint32_t mbiSize = 0;
    mbiSize = SerialGetByte();
    mbiSize |= (SerialGetByte() << 8);
    mbiSize |= (SerialGetByte() << 16);
    mbiSize |= (SerialGetByte() << 24);

    uint8_t *mbiLocn = (uint8_t *)((0x100000 - mbiSize) & 0xfffff000);

    if ((uint32_t)mbiLocn < 0xfc000 || mbiSize > 0x4000) {  // -- MUST be 16K or less
        SerialPutS("\x15\nMBI Information is too large\n");
        Halt();
    }

    // -- reset the status
    soFar = 0;
    error = false;
    mem = mbiLocn;

    // -- Ok, we are ready to receive
    SerialPutS("\x06");

    while (soFar < mbiSize) {
        *mem = SerialGetByte();
        ++mem;
        ++soFar;

        if (error) {
            // -- The server will check for a NAK between bytes
            SerialPutS("\x15\nError or timeout receiving mbi structure\n");
            Halt();
        }
    }

    SerialPutS("\x06");

    // -- now we need the entry address
    uint32_t entry = 0;
    entry = SerialGetByte();
    entry |= (SerialGetByte() << 8);
    entry |= (SerialGetByte() << 16);
    entry |= (SerialGetByte() << 24);
    kernel = (kernel_t)entry;

    // -- Send the final ACK that we are ready to boot
    SerialPutS("\x06");

    uint8_t ack = SerialGetByte();

    if (ack != '\x06') {
        SerialPutS("Expected an ACK character for permission to boot");
        Halt();
    }

    kernel(0x2badb002, (uint32_t)mbiLocn, atags);
}