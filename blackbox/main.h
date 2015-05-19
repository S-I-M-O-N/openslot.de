#ifndef MAIN_H
#define MAIN_H

#include <avr/wdt.h>
#include <stdint.h>

#define VERSION "1.8"

#define COUNTDOWN_DELAY         10 // x/10 seconds

// Hardware config
#define LAP_COUNTER_PORT        PORTB
#define LAP_COUNTER             PB2

#define MODUL_PORT              PORTD
#define MODUL_ST4               PD5
#define MODUL_ST6               PD6

#define RESPONSEWIRE_PORT       MODUL_PORT
#define RESPONSEWIRE_PIN        MODUL_ST4

#define I2C_PORT                PORTC
#define I2C_SCL                 PC0
#define I2C_SDA                 PC1

#define LED1_PORT               PORTA
#define LED2_PORT               PORTA
#define LED3_PORT               PORTC
#define LED4_PORT               PORTC
#define LED5_PORT               PORTC
#define LED1                    PA4
#define LED2                    PA5
#define LED3                    PC7
#define LED4                    PC2
#define LED5                    PC3

#define SW_PACECAR_PORT         PORTC
#define SW_START_PORT           PORTB
#define SW_FUEL_PORT            PORTB
#define SW_PACECAR              PC6
#define SW_FUEL                 PB0
#define SW_START                PB1

#define SPEAKER_PORT            PORTD
#define SPEAKER                 PD7

#define RAIL_POWER_PORT         PORTD
#define RAIL_POWER              PD4 // high = rails powered
#define RAIL_DETECT_PORT        PORTA
#define RAIL_DETECT             PA7 // low = short circuit detection

#define CONTROLLER_PORT         PORTB
#define CONTROLLER_MAX          360 // full throttle ADC value
#define CONTROLLER_DIVISOR      (uint8_t)(CONTROLLER_MAX/15)
#define CONTROLLER1_SW          PB4
#define CONTROLLER2_SW          PB5
#define CONTROLLER3_SW          PB6
#define CONTROLLER4_SW          PB7
#define CONTROLLER1_SPEED       0 // ADC channel #
#define CONTROLLER2_SPEED       1 // ADC channel #
#define CONTROLLER3_SPEED       2 // ADC channel #
#define CONTROLLER4_SPEED       3 // ADC channel #

// TIMING STUFF
/*
#define TIMER0_DIVISOR   8
#define VALUE_50US       0.45e-4
#define TIMER_50US       0xff - (uint8_t)(VALUE_50US * F_CPU/TIMER0_DIVISOR)
*/
/*
#define VALUE_7500NS     0.85e-2
#define TIMER_7500NS     0xffff - (uint16_t)(VALUE_7500NS * F_CPU/TIMER1_DIVISOR)
*/
//#define TIMER_7500NS     (uint16_t)(VALUE_7500NS * F_CPU/TIMER1_DIVISOR)
#define TIMER0_DIVISOR  8
#define VALUE0_250US    0.25e-3
#define TIMER0_250US    0xff - (uint8_t)(VALUE0_250US * F_CPU/TIMER0_DIVISOR)
#define TIMER0_2300NS   (uint8_t)(2300/250)

#define TIMER2_DIVISOR  8
#define VALUE2_50US     0.50e-4
#define TIMER2_50US     (uint8_t)(VALUE2_50US * F_CPU/TIMER2_DIVISOR)
#define VALUE2_250US    2.5e-4
#define TIMER2_250US    (uint8_t)((VALUE2_250US * F_CPU/TIMER2_DIVISOR) - 2)

#define TIMER1_DIVISOR  8
#define VALUE1_7500NS   0.75e-2
#define TIMER1_7500NS   (uint16_t)(VALUE1_7500NS * F_CPU/TIMER1_DIVISOR)
#define VALUE1_500NS    0.5e-3
#define TIMER1_500NS    (uint16_t)((VALUE1_500NS * F_CPU/TIMER1_DIVISOR) + 2) // MEASURE THIS EXACTLY WITH SCOPE

#define DDR(x) (*(&x - 1))      // address of data direction register of port x
#define PIN(x) (*(&x - 2))      // address of input register of port x


// constants
#define MAX_SLOTS       6

// external variabled
extern uint8_t mode;
extern uint8_t btn_start;
extern uint8_t old_start;
extern const char SHORTCUT[];
extern const char PANIC[];
extern const char RESUME[];

// function prototypes
void reset_vars(void);



// MAYBE USELESS STUFF:

#define uchar unsigned char
#define uint unsigned int

// Macro used to write to a single I/O pin
#define writeBit(port,bit,value) { if ((value)>0) (port) |= (1<<bit); else (port) &= ~(1<<bit); } 

// Macro used to read from a single I/O pin
#define readBit(port,bit) (((port) >> (bit)) & 1)

// we have internal AREF...
// #define EXT_AREF 1

#endif
