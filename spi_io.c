/*
 *  File: spi_io.c.example
 *  Author: Nelson Lombardo
 *  Year: 2015
 *  e-mail: nelson.lombardo@gmail.com
 *  License at the end of file.
 */

#include "spi_io.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <pico/time.h>
#include <pico/types.h>
#include "stdio.h"
#include "hardware/timer.h"

/******************************************************************************
 Module Public Functions - Low level SPI control functions
******************************************************************************/
static inline void cs_select(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 0);
    asm volatile("nop \n nop \n nop"); // FIXME
}

static inline void cs_deselect(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 1);
    asm volatile("nop \n nop \n nop"); // FIXME
}

static absolute_time_t spi_timer_expire;

void SPI_Init (void)
{
    spi_init(spi0, 1000 * 1000);
    gpio_set_function(PICO_DEFAULT_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);

    gpio_init(PICO_DEFAULT_SPI_CSN_PIN);
    gpio_set_dir(PICO_DEFAULT_SPI_CSN_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, 1);
}

BYTE SPI_RW (BYTE d)
{
    uint8_t recv = 0;
    spi_write_read_blocking(spi0, &d, &recv, 1);
    return recv;
}

void SPI_Release (void)
{
    BYTE dummy = 0xFF;
    for (int i = 0; i < 10; i++) {
        SPI_RW(dummy);  // Send 80 clock pulses (10 * 8 bits)
    }
}

inline void SPI_CS_Low (void)
{
    cs_select(PICO_DEFAULT_SPI_CSN_PIN);
}

inline void SPI_CS_High (void)
{
    cs_deselect(PICO_DEFAULT_SPI_CSN_PIN);
}

inline void SPI_Freq_High (void) {
    spi_set_baudrate(spi_default, 12 * 1000 * 1000); // 10 MHz
}

inline void SPI_Freq_Low (void) {
    spi_set_baudrate(spi_default, 400 * 1000); // 400 kHz
}

void SPI_Timer_On (WORD ms)
{
    spi_timer_expire = make_timeout_time_ms(ms);
}

inline BOOL SPI_Timer_Status (void)
{
    return absolute_time_diff_us(get_absolute_time(), spi_timer_expire) < 0 ? 0 : 1;
}

inline void SPI_Timer_Off (void)
{
    spi_timer_expire = get_absolute_time();
}

/*
The MIT License (MIT)

Copyright (c) 2015 Nelson Lombardo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
