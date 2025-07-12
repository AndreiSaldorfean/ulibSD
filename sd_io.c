/*
 *  File: sd_io.c
 *  Author: Nelson Lombardo
 *  Year: 2015
 *  e-mail: nelson.lombardo@gmail.com
 *  License at the end of file.
 */

#include "sd_io.h"
#include "spi_io.h"
#include "stdio.h"

#ifdef _M_IX86  // For use over x86
/*****************************************************************************/
/* Private Methods Prototypes - Direct work with PC file                     */
/*****************************************************************************/

/**
 * \brief Get the total numbers of sectors in SD card.
 * \param dev Device descriptor.
 * \return Quantity of sectors. Zero if fail.
 */
DWORD __SD_Sectors (SD_DEV* dev);

/*****************************************************************************/
/* Private Methods - Direct work with PC file                                */
/*****************************************************************************/

DWORD __SD_Sectors (SD_DEV *dev)
{
    if (dev->fp == NULL) return(0); // Fail
    else {
        fseek(dev->fp, 0L, SEEK_END);
        return (((DWORD)(ftell(dev->fp)))/((DWORD)512)-1);
    }
}
#else   // For use with uControllers
/******************************************************************************
 Private Methods Prototypes - Direct work with SD card
******************************************************************************/

/**
    \brief Simple function to calculate power of two.
    \param e Exponent.
    \return Math function result.
*/
DWORD __SD_Power_Of_Two(BYTE e);

/**
     \brief Assert the SD card (SPI CS low).
 */
inline void __SD_Assert (void);

/**
    \brief Deassert the SD (SPI CS high).
 */
inline void __SD_Deassert (void);

/**
    \brief Change to max the speed transfer.
    \param throttle
 */
void __SD_Speed_Transfer (BYTE throttle);

/**
    \brief Send SPI commands.
    \param cmd Command to send.
    \param arg Argument to send.
    \return R1 response.
 */
BYTE __SD_Send_Cmd(BYTE cmd, DWORD arg);

/**
    \brief Write a data block on SD card.
    \param dat Storage the data to transfer.
    \param token Inidicates the type of transfer (single or multiple).
 */
SDRESULTS __SD_Write_Block(SD_DEV *dev, void *dat, BYTE token);

/**
    \brief Get the total numbers of sectors in SD card.
    \param dev Device descriptor.
    \return Quantity of sectors. Zero if fail.
 */
DWORD __SD_Sectors (SD_DEV *dev);

/******************************************************************************
 Private Methods - Direct work with SD card
******************************************************************************/

DWORD __SD_Power_Of_Two(BYTE e)
{
    DWORD partial = 1;
    BYTE idx;
    for(idx=0; idx!=e; idx++) partial *= 2;
    return(partial);
}

inline void __SD_Assert(void){
    SPI_CS_Low();
}

inline void __SD_Deassert(void){
    SPI_CS_High();
}

void __SD_Speed_Transfer(BYTE throttle) {
    if(throttle == HIGH) SPI_Freq_High();
    else SPI_Freq_Low();
}

BYTE __SD_Send_Cmd(BYTE cmd, DWORD arg)
{
    BYTE crc, res;
    // ACMD«n» is the command sequense of CMD55-CMD«n»
    SD_PRINTF("cmd & 0x80= %d\n",(cmd&0x80));
    if(cmd & 0x80) {
        SD_PRINTF("acilea\n");
        cmd &= 0x7F;
        res = __SD_Send_Cmd(CMD55, 0);
        SD_PRINTF("send command res= %d\n", res);
        if (res > 1) return (res);
    }

    // Select the card
    __SD_Deassert();
    SPI_RW(0xFF);
    __SD_Assert();
    SPI_RW(0xFF);

    // Send complete command set
    SD_PRINTF("cmd= %d\n",cmd);
    SPI_RW(cmd);                        // Start and command index
    SPI_RW((BYTE)(arg >> 24));          // Arg[31-24]
    SPI_RW((BYTE)(arg >> 16));          // Arg[23-16]
    SPI_RW((BYTE)(arg >> 8 ));          // Arg[15-08]
    SPI_RW((BYTE)(arg >> 0 ));          // Arg[07-00]

    // CRC?
    crc = 0x01;                           // Dummy CRC and stop
    if(cmd == CMD0)   crc = 0x95;         // Valid CRC for CMD0(0)
    if(cmd == CMD8)   crc = 0x87;         // Valid CRC for CMD8(0x1AA)
    if(cmd == CMD55)  crc = 0x65;         // Valid CRC for CMD8(0x1AA)
    if(cmd == ACMD41) crc = 0x77;         // Valid CRC for CMD8(0x1AA)
    SPI_RW(crc);

    // Receive command response
    // Wait for a valid response in timeout of 5 milliseconds
    SPI_Timer_On(5);
    do {
        res = SPI_RW(0xFF);
        SD_PRINTF("SPI_RW res= %d\n",res);
    } while((res & 0x80)&&(SPI_Timer_Status()==TRUE));
    SPI_Timer_Off();
    // Return with the response value
    return(res);
}

SDRESULTS __SD_Write_Block(SD_DEV *dev, void *dat, BYTE token)
{
    WORD idx;
    BYTE line;
    // Send token (single or multiple)
    SPI_RW(token);
    // Single block write?
    if(token != 0xFD)
    {
        // Send block data
        for(idx=0; idx!=SD_BLK_SIZE; idx++) SPI_RW(*((BYTE*)dat + idx));
        /* Dummy CRC */
        SPI_RW(0xFF);
        SPI_RW(0xFF);
        // If not accepted, returns the reject error
        if((SPI_RW(0xFF) & 0x1F) != 0x05) return(SD_REJECT);
    }
#ifdef SD_IO_WRITE_WAIT_BLOCKER
    // Waits until finish of data programming (blocked)
    while(SPI_RW(0xFF)==0);
    return(SD_OK);
#else
    // Waits until finish of data programming with a timeout
    SPI_Timer_On(SD_IO_WRITE_TIMEOUT_WAIT);
    do {
        line = SPI_RW(0xFF);
    } while((line==0)&&(SPI_Timer_Status()==TRUE));
    SPI_Timer_Off();
#ifdef SD_IO_DBG_COUNT
    dev->debug.write++;
#endif
    if(line==0) return(SD_BUSY);
    else return(SD_OK);
#endif
}

DWORD __SD_Sectors (SD_DEV *dev)
{
    BYTE csd[16];
    BYTE idx;
    DWORD ss = 0;
    WORD C_SIZE = 0;
    BYTE C_SIZE_MULT = 0;
    BYTE READ_BL_LEN = 0;
    if(__SD_Send_Cmd(CMD9, 0)==0)
    {
        printf("cmd9\n");
        // Wait for response
        while (SPI_RW(0xFF) == 0xFF);
        for (idx=0; idx!=16; idx++) csd[idx] = SPI_RW(0xFF);

        for (int i = 0; i < 16; i++) {
            printf("csd[%d] = 0x%02X\n", i, csd[i]);
        }
        printf("Card type = 0x%02X\n", dev->cardtype);
        // Dummy CRC
        SPI_RW(0xFF);
        SPI_RW(0xFF);
        SPI_Release();
        if(dev->cardtype & SDCT_SD1)
        {
            ss = csd[0];
            // READ_BL_LEN[83:80]: max. read data block length
            READ_BL_LEN = (csd[5] & 0x0F);
            // C_SIZE [73:62]
            C_SIZE = (csd[6] & 0x03);
            C_SIZE <<= 8;
            C_SIZE |= (csd[7]);
            C_SIZE <<= 2;
            C_SIZE |= ((csd[8] >> 6) & 0x03);
            // C_SIZE_MULT [49:47]
            C_SIZE_MULT = (csd[9] & 0x03);
            C_SIZE_MULT <<= 1;
            C_SIZE_MULT |= ((csd[10] >> 7) & 0x01);
        }
        else if (dev->cardtype & SDCT_SD2)
        {
            // C_SIZE [69:48]
            C_SIZE = ((DWORD)(csd[7] & 0x3F) << 16) |
                     ((DWORD)csd[8] << 8) |
                     (DWORD)csd[9];

            if (dev->cardtype & SDCT_BLOCK) {
                // SDHC/SDXC card (block addressing)
                ss = C_SIZE + 1;  // Number of 512-byte sectors
            } else {
                // SDSC card (byte addressing — rare for SDv2)
                C_SIZE_MULT = ((csd[9] & 0x03) << 1) | ((csd[10] >> 7) & 0x01);
                READ_BL_LEN = csd[5] & 0x0F;
                ss = (C_SIZE + 1);
                ss <<= (C_SIZE_MULT + 2);
                ss >>= (READ_BL_LEN - 9);  // Convert to 512-byte sectors
            }
        }
        ss = (C_SIZE + 1) * 1024;
        // ss *= __SD_Power_Of_Two(C_SIZE_MULT + 2);
        // ss *= __SD_Power_Of_Two(READ_BL_LEN);
        // ss /= SD_BLK_SIZE;
        return (ss);
    } else return (0); // Error
}
#endif // Private methods for uC

/******************************************************************************
 Public Methods - Direct work with SD card
******************************************************************************/

SDRESULTS SD_Init(SD_DEV *dev)
{
#if defined(_M_IX86)    // x86
    dev->fp = fopen(dev->fn, "r+");
    if (dev->fp == NULL)
        return (SD_ERROR);
    else
    {
        dev->last_sector = __SD_Sectors(dev);
#ifdef SD_IO_DBG_COUNT
        dev->debug.read = 0;
        dev->debug.write = 0;
#endif
        return (SD_OK);
    }
#else   // uControllers
    BYTE n, cmd, ct, ocr[4];
    BYTE idx;
    BYTE init_trys;
    ct = 0;
    SD_PRINTF("entering sd_init()\n");

    for(init_trys=0; ((init_trys!=SD_INIT_TRYS)&&(!ct)); init_trys++)
    {
        SD_PRINTF("Attempt #%d\n", init_trys);
        // Initialize SPI for use with the memory card
        SPI_Init();

        // Power On step
        {
            /*
             * Power ON or card insersion
               After supply voltage reached above 2.2 volts, wait for one millisecond at least.
               Set SPI clock rate between 100 kHz and 400 kHz. Set DI and CS high and apply 74 or more clock pulses to SCLK.
               The card will enter its native operating mode and go ready to accept native command.
             * */
            SPI_CS_High();  //CS high
            SPI_Freq_Low(); // set spi to between 100 - 400 kHz

            // 80 dummy clocks
            for(idx = 0; idx != 10; idx++) SPI_RW(0xFF);
        }

        // 80 dummy clocks
        for(idx = 0; idx != 10; idx++) SPI_RW(0xFF);

        // Software reset
        /*
           Send a CMD0 with CS low to reset the card.
           The card samples CS signal on a CMD0 is received successfully.
           If the CS signal is low, the card enters SPI mode and responds R1 with In Idle State bit set (0x01).
           Since the CMD0 must be sent as a native command, the CRC field must have a valid value.
           When once the card enters SPI mode, the CRC feature is disabled and the command CRC and data CRC are not checked by the card,
           so that command transmission routine can be written with the hardcorded CRC value that valid for only CMD0 and CMD8 used in the initialization process.
           The CRC feature can also be switched on/off with CMD59.
         * */
        {
            SD_PRINTF("Sending CMD0...\n");
            BYTE r1 = 0;
            dev->mount = FALSE;
            SPI_Timer_On(500);
            // while (((r1 =__SD_Send_Cmd(CMD0, 0)) != 1)&&(SPI_Timer_Status()==TRUE));
            while ((r1 != 1) && (SPI_Timer_Status()==TRUE))
            {
                r1 = __SD_Send_Cmd(CMD0, 0);
                SD_PRINTF("r1= %d\n", r1);
            }
            SPI_Timer_Off();
        }

        // Idle state
        if (__SD_Send_Cmd(CMD0, 0) == 1) {
            // SD version 2?
            if (__SD_Send_Cmd(CMD8, 0x1AA) == 1) {
                SD_PRINTF("here1\n");
                // Get trailing return value of R7 resp
                for (n = 0; n < 4; n++) ocr[n] = SPI_RW(0xFF);

                for (n = 0; n < 4; n++)
                {
                    SD_PRINTF("%X ",ocr[n]);
                }
                SD_PRINTF("\n");
                // VDD range of 2.7-3.6V is OK?
                if ((ocr[2] == 0x01)&&(ocr[3] == 0xAA))
                {
                    BYTE r2 = 0xFF;
                    BYTE r3 = 0xFF;
                    // Wait for leaving idle state (ACMD41 with HCS bit)...
                    SD_PRINTF("__SD_Send_Cmd(41,1<<30)\n");
                    SPI_Timer_On(1000);
                    while (SPI_Timer_Status() == TRUE)
                    {
                        r2 = __SD_Send_Cmd(ACMD41, 1UL << 30);
                        // r2 = __SD_Send_Cmd(CMD1, 0);
                        SD_PRINTF("r2_here= %d\n",r2);
                        if(r2 == 0)
                            break;
                    }
                    SPI_Timer_Off();

                    SPI_Timer_On(1000);
                    while (SPI_Timer_Status() == TRUE)
                    {
                        r2 = __SD_Send_Cmd(ACMD41, 1UL << 30);
                        SD_PRINTF("r2_here= %d\n",r2);
                        if(r2 == 0)
                            break;
                    }
                    SPI_Timer_Off();

                    SD_PRINTF("r2 = %d\n", r2);
                    // CCS in the OCR?
                    r3 = __SD_Send_Cmd(CMD58, 0);
                    SD_PRINTF("r3 = %d\n", r3);
                    SD_PRINTF("Timer_status = %d\n", SPI_Timer_Status());
                    if (r3 == 0)
                    {
                        SD_PRINTF("init ct\n");
                        for (n = 0; n < 4; n++) ocr[n] = SPI_RW(0xFF);
                        // SD version 2?
                        ct = (ocr[0] & 0x40) ? SDCT_SD2 | SDCT_BLOCK : SDCT_SD2;
                    }
                    SD_PRINTF("init ct failed\n");
                    SD_PRINTF("r3 = %d\n", r2);
                }
            } else {
                SD_PRINTF("here\n");
                // SD version 1 or MMC?
                if (__SD_Send_Cmd(ACMD41, 0) <= 1)
                {
                    // SD version 1
                    ct = SDCT_SD1;
                    cmd = ACMD41;
                } else {
                    // MMC version 3
                    ct = SDCT_MMC;
                    cmd = CMD1;
                }
                // Wait for leaving idle state
                SPI_Timer_On(250);
                while((SPI_Timer_Status()==TRUE)&&(__SD_Send_Cmd(cmd, 0)));
                SPI_Timer_Off();
                if(SPI_Timer_Status()==FALSE) ct = 0;
                if(__SD_Send_Cmd(CMD59, 0))   ct = 0;   // Deactivate CRC check (default)
                if(__SD_Send_Cmd(CMD16, 512)) ct = 0;   // Set R/W block length to 512 bytes
            }
            SD_PRINTF("cmd 2 failed\n");
        }
        SD_PRINTF("cmd 1 failed\n");
    }

    if(ct) {
        dev->cardtype = ct;
        dev->mount = TRUE;
        dev->last_sector = __SD_Sectors(dev) - 1;

        UINT r3;
        r3 = __SD_Send_Cmd(CMD58, 0);

        if (r3 == 0) {
            for (n = 0; n < 4; n++) {
                ocr[n] = SPI_RW(0xFF);
                printf("OCR[%d] = 0x%02X\n", n, ocr[n]);
            }
        }
        printf("last_sector= %d\n",dev->last_sector);
#ifdef SD_IO_DBG_COUNT
        dev->debug.read = 0;
        dev->debug.write = 0;
#endif
        __SD_Speed_Transfer(HIGH); // High speed transfer
    }
    SPI_Release();
    return (ct ? SD_OK : SD_NOINIT);
#endif
}

SDRESULTS SD_Read(SD_DEV *dev, void *dat, DWORD sector, WORD ofs, WORD cnt)
{
#if defined(_M_IX86)    // x86
    // Check the sector query
    if((sector > dev->last_sector)||(cnt == 0)) return(SD_PARERR);
    if(dev->fp!=NULL)
    {
        if (fseek(dev->fp, ((512 * sector) + ofs), SEEK_SET)!=0)
            return(SD_ERROR);
        else {
            if(fread(dat, 1, (cnt - ofs),dev->fp)==(cnt - ofs))
            {
#ifdef SD_IO_DBG_COUNT
                dev->debug.read++;
#endif
                return(SD_OK);
            }
            else return(SD_ERROR);
        }
    } else {
        return(SD_ERROR);
    }
#else   // uControllers
    SDRESULTS res;
    BYTE tkn;
    WORD remaining;
    res = SD_ERROR;
    if ((sector > dev->last_sector)||(cnt == 0)) return(SD_PARERR);
    // Convert sector number to byte address (sector * SD_BLK_SIZE)
    if (__SD_Send_Cmd(CMD17, sector * SD_BLK_SIZE) == 0) {
        SPI_Timer_On(100);  // Wait for data packet (timeout of 100ms)
        do {
            tkn = SPI_RW(0xFF);
        } while((tkn==0xFF)&&(SPI_Timer_Status()==TRUE));
        SPI_Timer_Off();
        // Token of single block?
        if(tkn==0xFE) {
            // Size block (512 bytes) + CRC (2 bytes) - offset - bytes to count
            remaining = SD_BLK_SIZE + 2 - ofs - cnt;
            // Skip offset
            if(ofs) {
                do {
                    SPI_RW(0xFF);
                } while(--ofs);
            }
            // I receive the data and I write in user's buffer
            do {
                *(BYTE*)dat = SPI_RW(0xFF);
                dat++;
            } while(--cnt);
            // Skip remaining
            do {
                SPI_RW(0xFF);
            } while (--remaining);
            res = SD_OK;
        }
    }
    SPI_Release();
#ifdef SD_IO_DBG_COUNT
    dev->debug.read++;
#endif
    return(res);
#endif
}

#ifdef SD_IO_WRITE
SDRESULTS SD_Write(SD_DEV *dev, void *dat, DWORD sector)
{
#if defined(_M_IX86)    // x86
    // Query ok?
    if(sector > dev->last_sector) return(SD_PARERR);
    if(dev->fp != NULL)
    {
        if(fseek(dev->fp, SD_BLK_SIZE * sector, SEEK_SET)!=0)
            return(SD_ERROR);
        else {
            if(fwrite(dat, 1, SD_BLK_SIZE, dev->fp)==SD_BLK_SIZE)
            {
#ifdef SD_IO_DBG_COUNT
                dev->debug.write++;
#endif
                return(SD_OK);
            }
            else return(SD_ERROR);
        }
    } else return(SD_ERROR);
#else   // uControllers
    // Query ok?
    if(sector > dev->last_sector) return(SD_PARERR);
    // Single block write (token <- 0xFE)
    // Convert sector number to bytes address (sector * SD_BLK_SIZE)
    if(__SD_Send_Cmd(CMD24, sector * SD_BLK_SIZE)==0)
        return(__SD_Write_Block(dev, dat, 0xFE));
    else
        return(SD_ERROR);
#endif
}
#endif

SDRESULTS SD_Status(SD_DEV *dev)
{
#if defined(_M_IX86)
    return((dev->fp == NULL) ? SD_OK : SD_NORESPONSE);
#else
    return(__SD_Send_Cmd(CMD0, 0) ? SD_OK : SD_NORESPONSE);
#endif
}

// «sd_io.c» is part of:
/*----------------------------------------------------------------------------/
/  ulibSD - Library for SD cards semantics            (C)Nelson Lombardo, 2015
/-----------------------------------------------------------------------------/
/ ulibSD library is a free software that opened under license policy of
/ following conditions.
/
/ Copyright (C) 2015, ChaN, all right reserved.
/
/ 1. Redistributions of source code must retain the above copyright notice,
/    this condition and the following disclaimer.
/
/ This software is provided by the copyright holder and contributors "AS IS"
/ and any warranties related to this software are DISCLAIMED.
/ The copyright owner or contributors be NOT LIABLE for any damages caused
/ by use of this software.
/----------------------------------------------------------------------------*/

// Derived from Mister Chan works on FatFs code (http://elm-chan.org/fsw/ff/00index_e.html):
/*----------------------------------------------------------------------------/
/  FatFs - FAT file system module  R0.11                 (C)ChaN, 2015
/-----------------------------------------------------------------------------/
/ FatFs module is a free software that opened under license policy of
/ following conditions.
/
/ Copyright (C) 2015, ChaN, all right reserved.
/
/ 1. Redistributions of source code must retain the above copyright notice,
/    this condition and the following disclaimer.
/
/ This software is provided by the copyright holder and contributors "AS IS"
/ and any warranties related to this software are DISCLAIMED.
/ The copyright owner or contributors be NOT LIABLE for any damages caused
/ by use of this software.
/----------------------------------------------------------------------------*/
