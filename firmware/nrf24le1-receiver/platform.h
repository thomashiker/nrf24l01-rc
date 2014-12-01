#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#include <stdint.h>
#include <nrf24le1.h>


#define __SYSTICK_IN_MS 16

#define NUMBER_OF_CHANNELS 3
#define SERVO_PULSE_CENTER 1500 * 4 / 3
#define INITIAL_ENDPOINT_DELTA 250

#define TIMER_VALUE_US(x) (0xffff - ((uint32_t)(__SYSTEM_CLOCK / 1000) / 12 * x / 1000))


// ****************************************************************************
// IO pins: (nRF24LE1 module 15x21 mm with 32pin QFN)
//
// Note: the pin numbers in the brackets refer to the module pinout, not
// the nRF24LE1 IC pinout!
//
//            Our board             XR3100      HKR3000
//
// P0.0                             LED green
// P0.1  (4 ) LED                   LED red
// P0.2
// P0.3  (8 ) Tx                    BIND        SDA
// P0.4  (9 ) Rx                                SCL
// P0.5  (10) CH1 / FSCK            CH1         CH1
// P0.6  (12) BIND                              BIND
// P0.7  (13) CH2 / FMOSI           CH2         CH2
// P1.0  (14) CH3 / FMISO           CH3         CH3
// P1.1  (15) FCSN                  CH4         CH4
// P1.2                                         LED green
// P1.3                             SCL         LED red
// P1.4                             SDA
// P1.5
// P1.6
//
// 3.3V  (5 )
// GND   (11)
// PROG  (7 )
// RESET (19)
//
// NOTE: this firmware does not make use of the EEPROM on the HobbyKing
// receivers. Bind data is always stored in the NV memory of the nRF24LE1.
//
// ****************************************************************************


#if HARDWARE == XR3100
    #define GPIO_BIND P0_3
    #define GPIO_LED_GREEN P0_0
    #define GPIO_LED P0_1
    #define GPIO_CH1 P0_5
    #define GPIO_CH2 P0_7
    #define GPIO_CH3 P1_0
    #define GPIO_PPM P1_1

    #define GPIO_INIT() \
    P0 = 0; \
    P0DIR = 0x08;    /* P0.3 is inputs, rest outputs */ \
    P0CON = 0x53;    /* Enable pull-up on bind button P0.3 */ \
    P1 = 0; \
    P1DIR = 0x18;    /* P1.3 and P1.4 are inputs, rest outputs */ \
    P1CON = 0x53;    /* Enable pull-up on SDA P1.3 */ \
    P1CON = 0x54;    /* Enable pull-up on SCL P1.4 */

#elif HARDWARE == HKR3000
    #define GPIO_BIND P0_6
    #define GPIO_LED_GREEN P1_2
    #define GPIO_LED P1_3
    #define GPIO_CH1 P0_5
    #define GPIO_CH2 P0_7
    #define GPIO_CH3 P1_0
    #define GPIO_PPM P1_1

    #define GPIO_INIT() \
    P0 = 0; \
    P0DIR = 0x58;    /* P0.3, P0.4 and P0.6 are inputs, rest outputs */ \
    P0CON = 0x53;    /* Enable pull-up on SDA P0.3 */ \
    P0CON = 0x54;    /* Enable pull-up on SCL P0.4 */ \
    P0CON = 0x56;    /* Enable pull-up on bind button P0.6 */ \
    P1 = 0; \
    P1DIR = 0x00;    /* All P1 ports are output */ \

#else
    #define GPIO_BIND P0_6
    #define GPIO_LED P0_1
    #define GPIO_CH1 P0_5
    #define GPIO_CH2 P0_7
    #define GPIO_CH3 P1_0
    #define GPIO_PPM P1_1

    #define GPIO_INIT() \
    P0 = 0; \
    P0DIR = 0x40;    /* All P0 ports except P0.6 are output */ \
    P0CON = 0x56;    /* Enable pull-up on bind button P0.6 */ \
    P1 = 0; \
    P1DIR = 0x00;    /* All P1 ports are output */ \

#endif


// ****************************************************************************
// Timer allocation
//
// Timer 0 provides a systick every 16ms, which is the interval in
// which servo pulses are output
//
// Timer 1 generates individual servo pulses. Its interrupt runs at highest
// priority to guarantee accurate servo pulses.
//
// Timer 2 is the hop timer.
//
// ****************************************************************************


void delay_us(uint16_t microseconds);

#endif // __PLATFORM_H__
