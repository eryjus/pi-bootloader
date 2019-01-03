//===================================================================================================================
//
//  pbl-server.c -- This is the main entry for the Pi-bootloader server component
//
//          Copyright (c)  2018 -- Adam Clark
//          Licensed under the BEER-WARE License, rev42 (see LICENSE.md)
//
//  This file is very heavily inspired from raspbootin, which can be found here:
//  https://raw.githubusercontent.com/mrvn/raspbootin/master/raspbootcom/raspbootcom.cc, though I changed much of
//  that code to operate as a state machine.  The license from raspbootin is included here and I gratefully
//  acknowledge the contribution:
//
//      Copyright (C) 2013 Goswin von Brederlow <goswin-v-b@web.de>
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 3 of the License, or
//      (at your option) any later version.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
//  The `main()` function is just a big loop, checking for file descriptors that need attention and then acting
//  them.  It will keep track of the action that is being performed so that larger files can be read in blocks and
//  passed along to the serial port.  One key here is that the program will also act like a serial console, so
//  it MUST be resilient enough to fall back into that mode on any error, resetting the status.
//
//  NOTE:
//  Most of the variables in this program are global variables.  This is usually bad programming form, and in
//  this case the program might have been better implemented as a C++ class with all of the attributes private.
//  However, this is also a standalone program and not a library that will be imported into several other programs.
//  I made this choice knowing that, at least in this program, the code will never be linked anywhere else.  As a
//  matter of fact, I have every intention of keeping this to a 1-source-file program.
//
// ------------------------------------------------------------------------------------------------------------------
//
//     Date      Tracker  Version  Pgmr  Description
//  -----------  -------  -------  ----  ---------------------------------------------------------------------------
//  2018-Dec-26  Initial   0.0.1   ADCL  Initial version
//
//===================================================================================================================

#define _GNU_SOURCE

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>


//
// -- A reasonable limit on the number of config lines (kernel + modules) we can support
//    ----------------------------------------------------------------------------------
#define MAX_CONFIG_LINES        10
#define MAX_CFG_FILE_SIZE       (MAX_CONFIG_LINES * 256)


//
// -- ELF: The number of identifying bytes
//    ------------------------------------
#define ELF_NIDENT      16


//
// -- ELF: The following are the meanings of the different positions in the eIdent field
//    ----------------------------------------------------------------------------------
enum {
    EI_MAG0             = 0,    // File Identification
    EI_MAG1             = 1,
    EI_MAG2             = 2,
    EI_MAG3             = 3,
    EI_CLASS            = 4,    // File Class
    EI_DATA             = 5,    // Data Encoding
    EI_VERSION          = 6,    // File Version
    EI_OSABI            = 7,    // OS/ABI Identification
    EI_ABIVERSION       = 8,    // ABI Version
    EI_PAD              = 9,    // padding bytes in eIdent
};


//
// -- ELF: The following are the possible values for the ELF class, indicating what size the file objects
//    ---------------------------------------------------------------------------------------------------
enum {
    ELFCLASS_NONE       = 0,    // Invalid
    ELFCLASS_32         = 1,    // 32-bit objects
    ELFCLASS_64         = 2,    // 64-bit objects
};


//
// -- ELF: The following are the possible values for the ELF Data encoding (big- or little-endian)
//    --------------------------------------------------------------------------------------------
enum {
    ELFDATA_NONE        = 0,    // Invalid
    ELFDATA_LSB         = 1,    // Binary values are in little endian order
    ELFDATA_MSB         = 2,    // Binary values are in big endian order
};


//
// -- ELF: The following are the defined types
//    ----------------------------------------
enum {
    ET_NONE             = 0,    // No file type
    ET_REL              = 1,    // Relocatable file
    ET_EXEC             = 2,    // Executable file
    ET_DYN              = 3,    // Dynamic or Shared object file
    ET_CORE             = 4,    // Core file
    ET_LOOS             = 0xfe00, // Environment-specific use
    ET_HIOS             = 0xfeff,
    ET_LOPROC           = 0xff00, // Processor-specific use
    ET_HIPROC           = 0xffff,
};


//
// -- This is the ELF file header, located starting at byte 0
//    -------------------------------------------------------
typedef struct {
    uint8_t e_ident[ELF_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr_t;


//
// -- This is the ELF Program header -- intended to make loading the ELF file easier
//    ------------------------------------------------------------------------------
typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr_t;


//
// -- This is the type of config line we have
//    ---------------------------------------
typedef enum {
    NONE,
    KERNEL,
    MODULE,
} Line_t;


//
// -- This structure holds the final configuration line
//    -------------------------------------------------
typedef struct {
    Line_t type;            // what kind of line do we have?
    char *originalLine;     // this is the line that was read from the file
    char *fileName;         // this is the file name in the line
    int fd;                 // this is the file descriptor we will read
    int size;               // this is the bytes that will be sent for the file
    int padding;            // this will be the number of bytes that will be used to pad to 4K
    char basename[32];      // this is the name that will be offered to the mbi structure
} ConfigLine_t;


//
// -- This enum indicates the state of the server
//    -------------------------------------------
typedef enum {
    OPEN_DEV        = 0x1000,           // need to open the device
    EXIT            = 0xffff,           // exit requested
    REINIT          = 0x1001,           // reinitialize the server and start over
    TTY             = 0x1002,           // In tty mode; input is passed to stdio
    CONFIG          = 0x1003,           // Read the config
    CHECK           = 0x1004,           // Check the config file for valid keywords and files
    SEND_SIZE       = 0x1005,           // send the size and wait for confirmation
    SEND_KERNEL     = 0x1006,           // send the kernel to the rpi
    SEND_MODULES    = 0x1007,           // send the modules to the rpi
    SEND_MBI_SIZE   = 0x1008,           // send the size of the MBI structure
    SEND_MBI        = 0x1009,           // send the mbi itself
    SEND_ENTRY      = 0x100a,           // send the entry point to the rpi
} State_t;


//
// -- This is the Multiboot 1 information structure as defined by the spec and padded to 8K to be contiguous
//    ------------------------------------------------------------------------------------------------------
typedef union {
    struct MB1 {
        //
        // -- These flags indicate which data elements have valid data
        //    --------------------------------------------------------
        uint32_t flags;

        //
        // -- The basic memory limits are valid when flag 0 is set; these values are in kilobytes
        //    -----------------------------------------------------------------------------------
        uint32_t availLowerMem;
        uint32_t availUpperMem;

        //
        // -- The boot device when flag 1 is set
        //    ----------------------------------
        uint32_t bootDev;

        //
        // -- The command line for this kernel when flag 2 is set
        //    ---------------------------------------------------
        uint32_t cmdLine;

        //
        // -- The loaded module list when flag 3 is set
        //    -----------------------------------------
        uint32_t modCount;
        uint32_t modAddr;

        //
        // -- The ELF symbol information (a.out-type symbols are not supported) when flag 5 is set
        //    ------------------------------------------------------------------------------------
        uint32_t shdrNum;                 // may still be 0 if not available
        uint32_t shdrSize;
        uint32_t shdrAddr;
        uint32_t shdrShndx;

        //
        // -- The Memory Map information when flag 6 is set
        //    ---------------------------------------------
        uint32_t mmapLen;
        uint32_t mmapAddr;

        //
        // -- The Drives information when flag 7 is set
        //    -----------------------------------------
        uint32_t drivesLen;
        uint32_t drivesAddr;

        //
        // -- The Config table when flag 8 is set
        //    -----------------------------------
        uint32_t configTable;

        //
        // -- The boot loader name when flag 9 is set
        //    ---------------------------------------
        uint32_t bootLoaderName;

        //
        // -- The APM table location when bit 10 is set
        //    -----------------------------------------
        uint32_t apmTable;

        //
        // -- The VBE interface information when bit 11 is set
        //    ------------------------------------------------
        uint32_t vbeControlInfo;
        uint32_t vbeModeInfo;
        uint16_t vbeMode;
        uint16_t vbeInterfaceSeg;
        uint16_t vbeInterfaceOff;
        uint16_t vbeInterfaceLen;

        //
        // -- The FrameBuffer information when bit 12 is set
        //    ----------------------------------------------
        uint64_t framebufferAddr;
        uint32_t framebufferPitch;
        uint32_t framebufferWidth;
        uint32_t framebufferHeight;
        uint8_t framebufferBpp;
        uint8_t framebufferType;
        union {
            struct {
                uint8_t framebufferRedFieldPos;
                uint8_t framebufferRedMaskSize;
                uint8_t framebufferGreenFieldPos;
                uint8_t framebufferGreenMaskSize;
                uint8_t framebufferBlueFieldPos;
                uint8_t framebufferBlueMaskSize;
            };
            struct {
                uint32_t framebufferPalletAddr;
                uint16_t framebufferPalletNumColors;
            };
        };
    } MB1;
    uint8_t raw[8192];
} __attribute__((packed)) MB1_t;


//
// -- Memory Map entries, which will repeat (pointer points to mmapAddr)
//    ------------------------------------------------------------------
typedef struct Mb1MmapEntry_t {
    uint32_t mmapSize;
    uint64_t mmapAddr;
    uint64_t mmapLength;
    uint32_t mmapType;
} __attribute__((packed)) Mb1MmapEntry_t;


//
// -- This is the loaded modules block (which will repeat)
//    ----------------------------------------------------
typedef struct Mb1Mods_t {
    uint32_t modStart;
    uint32_t modEnd;
    uint32_t modIdent;
    uint32_t modReserved;
} __attribute__((packed)) Mb1Mods_t;


//
// -- In this program we will have several global variables passed between the functions
//    ----------------------------------------------------------------------------------
const char *dev;
const char *cfg;
struct termios oldTio, newTio;

//
// -- These global variables will be reset when the connection resets
int fdDev = -1;
int fdMax = 0;
State_t state = OPEN_DEV;         // start needing to reset the state
fd_set readSet, writeSet, exceptSet;
ConfigLine_t cfgLines[MAX_CONFIG_LINES];
char cfgFile[MAX_CFG_FILE_SIZE] = {0};
uint32_t entry = 0;                     // keep track of the kernel entry point
uint8_t elfHdr[4096] = {0};                      // this is a modest buffer size for 1 elf page
int elfSects = 0;
Elf32_Phdr_t *phdr = 0;
MB1_t mbi;
uint32_t mbiSize = sizeof(struct MB1);
uint32_t modLocation = 0;


//
// --  Handle the Ctrl-C to clean up properly
//     --------------------------------------
void SignalHandler(int sig)
{
    printf("Caught signal %d; exiting            \n", sig);
    exit(EXIT_SUCCESS);
}


//
// -- On normal exit, use this function to clean up
//    ---------------------------------------------
void Cleanup(void)
{
    if (fdDev != -1) close(fdDev);
    fdDev = -1;

    // -- restore settings for STDIN_FILENO
    if (isatty(STDIN_FILENO)) tcsetattr(STDIN_FILENO, TCSANOW, &oldTio);
}


//
// -- Print the usage information and then exit
//    -----------------------------------------
void PrintUsage(const char * const pgm)
{
    printf("\nUsage:\n");
    printf("  %s <dev> <cfg-file>\n", pgm);
    exit(EXIT_FAILURE);
}


//
// -- Make sure the command line is well formatted
//    --------------------------------------------
void ParseCommandLine(int argc, const char * const argv[])
{
    if (argc != 3) PrintUsage(argv[0]);
    dev = argv[1];
    cfg = argv[2];
}


//
// -- Initialize the MBI structure
//    ----------------------------
void InitMbi(void)
{
    memset(&mbi, 0, sizeof(mbi));
    mbiSize = sizeof(struct MB1);

    mbi.MB1.flags = (1<<3) | (1<<6);        // just module and memory info for now

    // -- local variable to fill in the data
    Mb1MmapEntry_t *mmap = (Mb1MmapEntry_t *)&mbi.raw[mbiSize];
    mmap->mmapAddr = 0;
    mmap->mmapLength = 0x3f000000;
    mmap->mmapSize = sizeof(Mb1MmapEntry_t) - 4;
    mmap->mmapType = 1;

    // -- just 1 block of available memory
    mbi.MB1.mmapAddr = 0xfe000 + mbiSize;
    mbi.MB1.mmapLen = sizeof(Mb1MmapEntry_t);
    mbiSize += sizeof(Mb1MmapEntry_t);

    // -- no modules for now; will be added dynamically
}

//
// -- Perform all the initialization steps
//    ------------------------------------
void Init(int argc, const char * const argv[])
{
    printf("pi-bootloader v0.0.1\n");
    printf("  (C) 2018 Adam Clark under the BEER-WARE license\n");
    printf("  (Portions copyright (C) 2013 Goswin von Brederlow under GNUGPL v3)\n");
    printf("  Please report bugs at https://github.com/eryjus/pi-bootloader\n");

    // -- get the command options first
    ParseCommandLine(argc, argv);

    // -- First, get the terminal settings for stdin so we have them on exit
    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &oldTio) == -1) {
            perror("tcgetattr");
            exit(EXIT_FAILURE);
        }
    }

    // -- set up for a clean exit; from this point on will reset terminal settings
    atexit(Cleanup);
    signal(SIGINT, SignalHandler);

    // -- we need to save the old setting to restore, but copy them for our use
    newTio = oldTio;

    // -- disable canonical mode (buffered i/o) and local echo
    newTio.c_lflag &= (~ICANON & ~ECHO);

    // -- set the new settings immediately
    if (tcsetattr(STDIN_FILENO, TCSANOW, &newTio) == -1) {
        perror("tcsetattr()");
        exit(EXIT_FAILURE);
    }

    // -- initialize the config lines
    for (int i = 0; i < MAX_CONFIG_LINES; i ++) {
        cfgLines[i].type = NONE;
        cfgLines[i].originalLine = NULL;
        cfgLines[i].fileName = NULL;
        cfgLines[i].fd = -1;
        cfgLines[i].size = 0;
    }

    InitMbi();
}


//
// -- Perform the low-level work to open the serial device and prepare it
//    -------------------------------------------------------------------
static void _OpenDev(void)
{
    struct termios termios;     // -- The termios structure, to be configured for serial interface

    // -- Open the device, read/write, not the controlling tty, and non-blocking I/O
    fdDev = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (fdDev == -1) return;

    // -- must be a tty
    if (!isatty(fdDev)) {
        fprintf(stderr, "%s is not a tty\n", dev);
        exit(EXIT_FAILURE);
    }

    // -- Get the attributes
    if(tcgetattr(fdDev, &termios) == -1) {
        perror("Failed to get attributes of device");
        exit(EXIT_FAILURE);
    }

    // -- use polling
    termios.c_cc[VTIME] = 0;
    termios.c_cc[VMIN] = 0;

    // -- 8N1 mode, no input/output/line processing masks
    termios.c_iflag = 0;
    termios.c_oflag = 0;
    termios.c_cflag = CS8 | CREAD | CLOCAL;
    termios.c_lflag = 0;

    // -- Set the baud rate
    if((cfsetispeed(&termios, B115200) < 0) || (cfsetospeed(&termios, B115200) < 0)) {
        perror("Failed to set baud-rate");
        exit(EXIT_FAILURE);
    }

    // -- Write the attributes
    if (tcsetattr(fdDev, TCSAFLUSH, &termios) == -1) {
        perror("tcsetattr()");
        exit(EXIT_FAILURE);
    }
}


//
// -- Reinitialize the variables for reset
//    ------------------------------------
void Reinit(void)
{
    fprintf(stderr, "\n### Listening to %s...      \n", dev);

    // -- Set fdDev non-blocking
    if (fcntl(fdDev, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl()");
        close(fdDev);
        state = OPEN_DEV;
        return;
    }

    // -- select needs the largest FD + 1
    fdMax = (fdDev>STDIN_FILENO?fdDev+1:STDIN_FILENO+1);

    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    FD_ZERO(&exceptSet);

    // -- clear out the config lines
    for (int i = 0; i < MAX_CONFIG_LINES; i ++) {
        if (cfgLines[i].fd != -1) close(cfgLines[i].fd);
        cfgLines[i].type = NONE;
        cfgLines[i].originalLine = NULL;
        cfgLines[i].fileName = NULL;
        cfgLines[i].fd = -1;
        cfgLines[i].size = 0;
    }

    // -- clear out the config file and elf hdr
    memset(cfgFile, 0, MAX_CFG_FILE_SIZE);
    memset(elfHdr, 0, MAX_CFG_FILE_SIZE);
    InitMbi();

    // -- reset the entry point and elf data
    entry = 0;
    elfSects = 0;
    phdr = 0;

    // -- we have reached this point and have a connection to the serial port; now we need to get into tty mode
    state = TTY;
}


//
// -- Open up the device and prepare it to be used -- initialize everything required
//    ------------------------------------------------------------------------------
void OpenDev(void)
{
    if (fdDev != -1) close(fdDev);
    fdDev = -1;
    fdMax = 0;

    while (1) {
        _OpenDev();
        if (fdDev == -1) {
            // -- udev takes a while to change ownership so sometimes one gets EPERM
            if (errno == ENOENT || errno == ENODEV || errno == EACCES) {
                fprintf(stderr, "\r### Waiting for %s...\r", dev);
                sleep(1);
                continue;
            }

            // -- we had some other error we cannot handle
            perror(dev);
            exit(EXIT_FAILURE);
        } else break;
    }

    Reinit();           // -- perform the variable initialization
}


//
// -- Act as a TTY Terminal
//    ---------------------
void DoTty(void)
{
    const int BUF_SIZE = 1024;      // -- create a modest buffer for TTY data
    char buf[BUF_SIZE];

    // -- first reset the FDs we are interested in
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    FD_ZERO(&exceptSet);

    // -- FDs to read from (we are not transferring so no need to look for room to write)
    FD_SET(STDIN_FILENO, &readSet);
    FD_SET(fdDev, &readSet);

    // -- FDs to watch for error
    FD_SET(STDIN_FILENO, &exceptSet);
    FD_SET(fdDev, &exceptSet);

    while (1) {
        int breaks = 0;
        bool didSomething = false;

        // -- block until we have something to do
        if (select(fdMax, &readSet, NULL, &exceptSet, NULL) == -1) {
            // -- if we get some error, assume we need to reset
            perror("select() function -- resetting");
            state = REINIT;
            return;
        }

        // -- is stdin in error?
        if (FD_ISSET(STDIN_FILENO, &exceptSet)) {
            fprintf(stderr, "unrecoverable error on STDIN\n");
            exit(EXIT_FAILURE);
        }

        // -- did we have a problem with the dev?
        if (FD_ISSET(fdDev, &exceptSet)) {
            fprintf(stderr, "error on %s -- resetting\n", dev);
            state = REINIT;
            return;
        }

        // -- input from the user, copy to RPi
        if (FD_ISSET(STDIN_FILENO, &readSet)) {
            ssize_t len = read(STDIN_FILENO, buf, BUF_SIZE);        // read as much as we can

            // -- len may be -1, 0, or some number of bytes;
            if (len == -1) {
                perror("read() on STDIN");
                exit(EXIT_FAILURE);
            }

            len = write(fdDev, buf, len);

            if (len == -1) {
                perror("write() to tty");
                state = REINIT;
                return;
            }

            didSomething = true;
        }

        // -- output from the RPi, copy to STDOUT
        if (FD_ISSET(fdDev, &readSet)) {
            ssize_t len = read(fdDev, buf, BUF_SIZE);
            const char *ptr = buf;

            if (len < 1) {          // if we don't get any data, treat it like an error
                perror("read() from tty");
                state = REINIT;
                return;
            }

            // -- we need to scan this rpi output for a triple break.  Start with a single one
            while (ptr < &buf[len]) {
                const char *brk = index(ptr, '\x03');

                if (brk == NULL) brk = &buf[len];
                if (ptr == brk) {
                    ++breaks;
                    ++ptr;

                    if (breaks == 3) {
                        if (ptr != &buf[len]) {
                            fprintf(stderr, "Discarding input after tripple break\n");
                        }

                        // -- here we change into read the config mode
                        fprintf(stderr, "Preparing to send %s data\n", cfg);
                        state = CONFIG;
                        return;
                    }
                } else {
                    while (breaks > 0) {
                        ssize_t len2 = write(STDOUT_FILENO, "\x03\x03\x03", breaks);
                        if (len2 == -1) {
                            perror("write() to stdout");
                            exit(EXIT_FAILURE);
                        }

                        breaks -= len2;
                    }

                    while (ptr < brk) {
                        ssize_t len2 = write(STDOUT_FILENO, ptr, brk - ptr);
                        if (len2 == -1) {
                            perror("write() to stdout");
                            exit(EXIT_FAILURE);
                        }

                        ptr += len2;
                    }
                }
            }

            didSomething = true;
        }

        if (!didSomething) {
            state = REINIT;
            return;
        }
    }
}


//
// -- Read the configuration file and complete all the necessary validations
//    ----------------------------------------------------------------------
void ReadConfig(void)
{
    int fdCfg = open(cfg, O_RDONLY);
    int ln = 0;

    if (fdCfg == -1) {
        perror(cfg);
        state = REINIT;
        return;
    }

    int len = read(fdCfg, cfgFile, MAX_CFG_FILE_SIZE);
    close(fdCfg);

    if (len == MAX_CFG_FILE_SIZE) {
        fprintf(stderr, "Config file buffer overrun\n");
        state = REINIT;
        return;
    }

    // -- separate the lines into each own pointer
    bool setPtr = true;
    for (int i = 0; i < len; i ++) {
        char ch = cfgFile[i];
        if (ch == '\n' || ch == '\r') {
            // -- null out the CR/LF chars
            cfgFile[i] = 0;
            setPtr = true;
        } else if (setPtr && (ch == '\t' || ch == ' ')) {
            // -- skip leading ws
            cfgFile[i] = 0;
        } else if (setPtr) {
            cfgLines[ln++].originalLine = &cfgFile[i];
            setPtr = false;
        }

        if (ln == MAX_CONFIG_LINES) {
            fprintf(stderr, "Too many lines in %s; only %d lines supported\n", cfg, MAX_CONFIG_LINES);
            state = REINIT;
            return;
        }
    }

    state = CHECK;
}


//
// -- Trim a line of trailing blanks and return just the file name
//    ------------------------------------------------------------
char *Trim(char *var)
{
    int pos = 6;                                                // -- skip past the keyword
    if (!var) return NULL;
    while (var[pos] == ' ' || var[pos] == '\t') pos++;
    int ll = strlen(var) - 1;
    while (var[ll] == ' ' || var[ll] == '\t') var[ll--] = 0;

    return var + pos;
}


//
// -- Parse an elf kernel file, and adjust the byte count properly for the bss and header
//    -----------------------------------------------------------------------------------
void ParseElf(void)
{
    int byteCnt = 0;
    const int fd = cfgLines[0].fd;          // just to make the code a little easier to read

    if (read(fd, elfHdr, 4096) != 4096) {
        perror("Kernel ELF not big enough\n");
        state = REINIT;
        return;
    }

    Elf32_Ehdr_t *ehdr = (Elf32_Ehdr_t *)elfHdr;

    if (ehdr->e_ident[0] != '\x7f' || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L'
            || ehdr->e_ident[3] != 'F') {
        fprintf(stderr, "Bad ELF Signature\n");
        state = REINIT;
        return;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS_32) {
        fprintf(stderr, "Module is not 32-bit\n");
        state = REINIT;
        return;
    }

    if (ehdr->e_ident[EI_DATA] != ELFDATA_LSB) {
        fprintf(stderr, "Module is not in Little Endian byte order\n");
        state = REINIT;
        return;
    }

    if (ehdr->e_type != ET_EXEC) {
        fprintf(stderr, "Module is not an executable file\n");
        state = REINIT;
        return;
    }

    entry = ehdr->e_entry;

    phdr = (Elf32_Phdr_t *)((char *)ehdr + ehdr->e_phoff);
    elfSects = ehdr->e_phnum;

    for (int i = 0; i < elfSects; i ++) {
        byteCnt += phdr[i].p_memsz;
        if (byteCnt & 0xfff) byteCnt = (byteCnt & 0xfffff000) + 0x1000;
    }

    cfgLines[0].size = byteCnt;
    cfgLines[0].padding = 0;                // we took care of that in this function
}


//
// -- Check the config file to make sure it is valid
//    ----------------------------------------------
void CheckConfig(void)
{
    for (int i = 0; i < MAX_CONFIG_LINES; i ++) {
        // -- no need to process an empty line
        if (cfgLines[i].originalLine == NULL) continue;

        // -- determine the keyword for the line
        if (strncmp(cfgLines[i].originalLine, "kernel", 6) == 0) cfgLines[i].type = KERNEL;
        else if (strncmp(cfgLines[i].originalLine, "module", 6) == 0) cfgLines[i].type = MODULE;
        else {
            fprintf(stderr, "config file line %d has an invalid keyword: %6s\n", i + 1, cfgLines[i].originalLine);
            state = REINIT;
            return;
        }

        // now calidate the keyword for this line: i == 0 can only be kernel; 1 to (MAX_CONFIG_LINES-1) only module
        if (i == 0 && cfgLines[i].type != KERNEL) {
            fprintf(stderr, "The top config line must contain the 'kernel' keyword\n");
            state = REINIT;
            return;
        }

        if (i > 0 && cfgLines[i].type != MODULE) {
            fprintf(stderr, "All lines but the top line must contain the 'module' keyword\n");
            state = REINIT;
            return;
        }

        // -- isolate the file name
        cfgLines[i].fileName = Trim(cfgLines[i].originalLine);
        strcpy(cfgLines[i].basename, basename(cfgLines[i].fileName));

        if (cfgLines[i].fileName == NULL) {
            fprintf(stderr, "config file %s has an empty file name on line %d\n", cfg, i + 1);
            state = REINIT;
            return;
        }

        // -- now open the file
        cfgLines[i].fd = open(cfgLines[i].fileName, O_RDONLY);
        if (cfgLines[i].fd == -1) {
            perror(cfgLines[i].fileName);
            state = REINIT;
            return;
        }

        // -- check the size
        cfgLines[i].size = lseek(cfgLines[i].fd, 0L, SEEK_END);
        lseek(cfgLines[i].fd, 0L, SEEK_SET);
        if (cfgLines[i].size == 0) {
            fprintf(stderr, "Empty file %s cannot be sent\n", cfgLines[i].fileName);
            state = REINIT;
            return;
        }

        // -- adjsut the size up to the next 4K
        if (cfgLines[i].size & 0xfff) cfgLines[i].padding = 0x1000 - (cfgLines[i].size & 0xfff);
    }

    // -- now, go read some of the kernel and fix the size up
    ParseElf();

    state = SEND_SIZE;
}


//
// -- Send the size of all the modules we expect to send
//    --------------------------------------------------
void SendSize(void)
{
    int totalSize = 0;
    char *sz = (char *)&totalSize;
    int i;
    char resp;

    for (i = 0; i < MAX_CONFIG_LINES; i ++) {
        totalSize += (cfgLines[i].size + cfgLines[i].padding);
    }

    fprintf(stderr, "Notifying the RPi that %d bytes will be sent\n", totalSize);

    // -- Set fdDev blocking
    if (fcntl(fdDev, F_SETFL, 0) == -1) {
	    perror("fcntl()");
	    state = REINIT;
        return;
    }

    // -- Send the size
    if (write(fdDev, sz, 4) == -1) {
        perror(dev);
        state = REINIT;
        return;
    }

    // -- wait for a character
    int cnt;
    while ((cnt = read(fdDev, &resp, 1)) == 0) {
        if (cnt == -1) {
            perror(dev);
            state = REINIT;
            return;
        }
        sleep(1);
    }

    if (resp != '\x06') {
        fprintf(stderr, "The size %d is too big for the pi\n", totalSize);
        state = REINIT;
        return;
    }

    modLocation = 0x100000 + totalSize;
    state = SEND_KERNEL;
}


//
// -- Send the kernel to the pi, as a prepared elf file
//    -------------------------------------------------
void SendKernel(void)
{
    #define bufSize   1024*64
    static uint8_t kBuf[bufSize];       // 64 K buffer on the .bss section
    int bytesSent = 0;
    fprintf(stderr, "Sending kernel...\r");

    // -- Set fdDev blocking
    if (fcntl(fdDev, F_SETFL, 0) == -1) {
        perror("fcntl()");
        state = REINIT;
        return;
    }

    // -- there are several sections to send
    for (int i = 0; i < elfSects; i ++) {
        int sectBytes = phdr[i].p_filesz;

        if (lseek(cfgLines[0].fd, phdr[i].p_offset, SEEK_SET) == -1) {
            perror(cfgLines[0].fileName);
            state = REINIT;
            return;
        }

        while (sectBytes > 0) {
            int bytes = read(cfgLines[0].fd, kBuf, (sectBytes>bufSize?bufSize:sectBytes));
            if (bytes == -1) {
                perror("read() kernel");
                state = REINIT;
                return;
            }

            int res = write(fdDev, kBuf, bytes);
            if (res == -1) {
                perror("sect write() to dev");
                state = REINIT;
                return;
            }

            sectBytes -= bytes;
            bytesSent += bytes;
            fprintf(stderr, "Sending kernel (%d bytes sent)...\r", bytesSent);
        }

        sectBytes = phdr[i].p_memsz - phdr[i].p_filesz;
        if (phdr[i].p_memsz & 0xfff) sectBytes += (0x1000 - (phdr[i].p_memsz & 0xfff));

        memset(kBuf, 0, bufSize);

        while (sectBytes > 0) {
            int bytes = (sectBytes>bufSize?bufSize:sectBytes);
            int res = write(fdDev, kBuf, bytes);
            if (res == -1) {
                perror("padding write() to dev");
                state = REINIT;
                return;
            }

            sectBytes -= bytes;
            bytesSent += bytes;
            fprintf(stderr, "Sending kernel (%d bytes sent)...\r", bytesSent);
        }
    }

    state = SEND_MODULES;
    fprintf(stderr, "The kernel has been sent                      \n");
    #undef bufSize
}


//
// -- Send the kernel entry point to the rpi
//    --------------------------------------
void SendEntry(void)
{
    char *b = (char *)&entry;
    fprintf(stderr, "Sending the Entry point as %x\n", entry);

    int res = write(fdDev, b, 4);
    if (res == -1) {
        perror("entry point write()");
        state = REINIT;
        return;
    }

    char ack;

    while (read(fdDev, &ack, 1) == 0) {}
    if (ack != '\x06') {
        fprintf(stderr, "failed to get ACK after entry");
        state = REINIT;
        return;
    }

    fprintf(stderr, "Waiting for the rpi to boot\n");
    state = TTY;
}


//
// -- Send the number of bytes for the MBI structure
//    ----------------------------------------------
void SendMbiSize(void)
{
    mbiSize = 8192;             // we need the whole structure based on the string locations
    char *sz = (char *)&mbiSize;
    char resp;

    // -- Set fdDev blocking
    if (fcntl(fdDev, F_SETFL, 0) == -1) {
	    perror("fcntl()");
	    state = REINIT;
        return;
    }

    // -- Send the size
    if (write(fdDev, sz, 4) == -1) {
        perror(dev);
        state = REINIT;
        return;
    }

    // -- wait for a character
    int cnt;
    while ((cnt = read(fdDev, &resp, 1)) == 0) {
        if (cnt == -1) {
            perror(dev);
            state = REINIT;
            return;
        }
        sleep(1);
    }

    if (resp != '\x06') {
        fprintf(stderr, "The size %d is too big for the pi\n", mbiSize);
        state = REINIT;
        return;
    }

    state = SEND_MBI;
}


//
// -- Send the mbi to the kernel
//    --------------------------
void SendMbi(void)
{
    char ack;

    int res = write(fdDev, &mbi, mbiSize);
    if (res == -1) {
        perror("mbi write() to dev");
        state = REINIT;
        return;
    }

    while ((res = read(fdDev, &ack, 1)) == 0) { }

    if (res == -1) {
        perror("Get Ack after mbi\n");
        state = REINIT;
        return;
    }

    if (ack != '\x06') {
        fprintf(stderr, "Never got ACK after mbi\n");
    }

    // -- Set fdDev non-blocking
    if (fcntl(fdDev, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl()");
        state = REINIT;
        return;
    }

    state = SEND_ENTRY;
}


//
// -- Send the modules to the rpi
//    ---------------------------
void SendModules(void)
{
    #define bufSize   1024*64
    mbi.MB1.modAddr = 0xfe000 + mbiSize;
    mbi.MB1.modCount = 0;
    Mb1Mods_t *modArray = (Mb1Mods_t *)&mbi.raw[mbiSize];

    for (int m = 1; m < MAX_CONFIG_LINES; m ++) {
        if (cfgLines[m].type == NONE) continue;

        // -- update the mbi with this module information
        modArray[mbi.MB1.modCount].modStart = modLocation;
        modArray[mbi.MB1.modCount].modEnd = modLocation + cfgLines[m].size;
        modArray[mbi.MB1.modCount].modIdent = (uint32_t)(0x100000 - 34 - (m * 34));
        strcpy((char *)&mbi.raw[8192 - 34 - (m * 34)], cfgLines[m].basename);
        modLocation += cfgLines[m].size;
        mbiSize += sizeof(Mb1Mods_t);
        mbi.MB1.modCount ++;

        static uint8_t mBuf[bufSize];       // 64 K buffer on the .bss section
        int bytesSent = 0;

        // -- Set fdDev blocking
        if (fcntl(fdDev, F_SETFL, 0) == -1) {
            perror("fcntl()");
            state = REINIT;
            return;
        }

        int sectBytes = cfgLines[m].size;

        if (lseek(cfgLines[m].fd, 0, SEEK_SET) == -1) {
            perror(cfgLines[m].fileName);
            state = REINIT;
            return;
        }

        while (sectBytes > 0) {
            int bytes = read(cfgLines[m].fd, mBuf, (sectBytes>bufSize?bufSize:sectBytes));
            if (bytes == -1) {
                perror("read() modules");
                state = REINIT;
                return;
            }

            int res = write(fdDev, mBuf, bytes);
            if (res == -1) {
                perror("sect write() to dev");
                state = REINIT;
                return;
            }

            sectBytes -= bytes;
            bytesSent += bytes;
            fprintf(stderr, "Sending module %s (%d bytes sent)...\r", cfgLines[m].basename, bytesSent);
        }

        sectBytes = cfgLines[m].padding;
        memset(mBuf, 0, bufSize);

        while (sectBytes > 0) {
            int bytes = (sectBytes>bufSize?bufSize:sectBytes);
            int res = write(fdDev, mBuf, bytes);
            if (res == -1) {
                perror("padding write() to dev");
                state = REINIT;
                return;
            }

            sectBytes -= bytes;
            bytesSent += bytes;
            fprintf(stderr, "Sending module %s (%d bytes sent)...\r", cfgLines[m].basename, bytesSent);
        }
    }

    fprintf(stderr, "\rDone                                                                        \n");

    char ack;
    int res;

    while ((res = read(fdDev, &ack, 1)) == 0) { }

    if (res == -1) {
        perror("Get Ack after kernel/modules\n");
        state = REINIT;
        return;
    }

    if (ack != '\x06') {
        fprintf(stderr, "Never got ACK after kernel/modules\n");
    }

    // -- Set fdDev non-blocking
    if (fcntl(fdDev, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl()");
        state = REINIT;
        return;
    }

    state = SEND_MBI_SIZE;
    #undef bufSize
}


//
// -- This is the main entry point
//    ----------------------------
int main(int argc, const char * const argv[])
{
    Init(argc, argv);

    while(state != EXIT) {
        switch (state) {
        case EXIT:                  // -- technically this should never happen, but loop to exit
            continue;

        case OPEN_DEV:
            OpenDev();              // -- open the device
            break;

        case REINIT:
            state = OPEN_DEV;
            break;

        case TTY:
            DoTty();                // -- act at a TTY and pass data to/from the serial device
            break;

        case CONFIG:
            ReadConfig();           // -- read the config file and determine if passes edits
            break;

        case CHECK:
            CheckConfig();          // -- check that everything in the config is valid
            break;

        case SEND_SIZE:
            SendSize();             // -- send the size and check to make sure the pi can handle it
            break;

        case SEND_KERNEL:
            SendKernel();           // -- send the kernel to the rpi (an elf that is decomposed)
            break;

        case SEND_MODULES:
            SendModules();          // -- send the modules to the rpi (a file as-is, but padded to 4K)
            continue;

        case SEND_MBI_SIZE:
            SendMbiSize();          // -- send the multiboot information structure size
            break;

        case SEND_MBI:
            SendMbi();              // -- send the mbi itself
            break;

        case SEND_ENTRY:
            SendEntry();            // -- send the entry point to the rpi
            break;

        default:
            break;
        }
    }
}



