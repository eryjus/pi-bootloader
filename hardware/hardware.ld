/*******************************************************************************************************************/
/*                                                                                                                 */
/*  hardware.ld -- This is the linker script for locating the sections in the target binary                        */
/*                                                                                                                 */
/*        Copyright (c)  2017-2018 -- Adam Clark                                                                   */
/*        Licensed under the BEER-WARE License, rev42 (see LICENSE.md)                                             */
/*                                                                                                                 */
/* --------------------------------------------------------------------------------------------------------------- */
/*                                                                                                                 */
/*     Date      Tracker  Version  Pgmr  Description                                                               */
/*  -----------  -------  -------  ----  ------------------------------------------------------------------------  */
/*  2018-Dec-25  Initial   0.0.1   ADCL  Initial version                                                           */
/*                                                                                                                 */
/*******************************************************************************************************************/


ENTRY(_start)

SECTIONS {
    . = 0x8000;  

    .text : {
        *(.entry)
        *(.text)
        *(.rodata)
    }
    . = ALIGN(4096);

    .data : {
        *(.data)
    }
    . = ALIGN(4);       /* should not be needed, but just in case */
    
    _bssStart = .;
    .bss : {
        *(.bss)
    }
    _bssEnd = .;
}
