#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdbool.h>
#include "stdint.h"
#include "main.h"
#include "driver/adc.h"
#include "driver/rs232.h"
#include "driver/i2clcd.h"
#include "driver/i2cmaster.h"
#include "lowlevel.h"

extern uint8_t  btn_p = _BV(SW_PACECAR);
extern uint8_t  old_p = _BV(SW_PACECAR);

void blinkdelay(void) {
   _delay_ms(50);
}

void LED(uint8_t num, uint8_t state) {
    switch (num) {
        case 1: switch (state) {
            case 0: LED1_PORT &= ~_BV(LED1); break;
            case 1: LED1_PORT |= _BV(LED1); break;
            case 2: LED1_PORT ^= _BV(LED1); break;
            } break;

        case 2: switch (state) {
            case 0: LED2_PORT &= ~_BV(LED2); break;
            case 1: LED2_PORT |= _BV(LED2); break;
            case 2: LED2_PORT ^= _BV(LED2); break;
            } break;

        case 3: switch (state) {
            case 0: LED3_PORT &= ~_BV(LED3); break;
            case 1: LED3_PORT |= _BV(LED3); break;
            case 2: LED3_PORT ^= _BV(LED3); break;
            } break;

        case 4: switch (state) {
            case 0: LED4_PORT &= ~_BV(LED4); break;
            case 1: LED4_PORT |= _BV(LED4); break;
            case 2: LED4_PORT ^= _BV(LED4); break;
            } break;

        case 5: switch (state) {
            case 0: LED5_PORT &= ~_BV(LED5); break;
            case 1: LED5_PORT |= _BV(LED5); break;
            case 2: LED5_PORT ^= _BV(LED5); break;
            } break;
    }
}

void LEDS_OFF(void) {
    LED1_PORT &= ~_BV(LED1);
    LED2_PORT &= ~_BV(LED2);
    LED3_PORT &= ~_BV(LED3);
    LED4_PORT &= ~_BV(LED4);
    LED5_PORT &= ~_BV(LED5);
}

void init_hardware(void) {
    // reset all ports to input, no pullup
    DDRA = 0; PORTA = 0;
    DDRB = 0; PORTB = 0;
    DDRC = 0; PORTC = 0;
    DDRD = 0; PORTD = 0;


    RS232_init(); // initialize RS232 interface
    initADC();
    SFIOR = 0;

  	i2c_init();	// initialize i2c bus and lcd display
  	lcd_init();
  	lcd_command(LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKINGOFF);
	lcd_light(true);

    // set LED output
    DDR(LED1_PORT) |= _BV(LED1);
    DDR(LED2_PORT) |= _BV(LED2);
    DDR(LED3_PORT) |= _BV(LED3);
    DDR(LED4_PORT) |= _BV(LED4);
    DDR(LED5_PORT) |= _BV(LED5);

    // set SPEAKER output
    DDR(SPEAKER_PORT) |= _BV(SPEAKER);

    // set Controller Input Pull-UPs
//    CONTROLLER_PORT |= (_BV(CONTROLLER1_SW) | _BV(CONTROLLER2_SW) | _BV(CONTROLLER3_SW) | _BV(CONTROLLER4_SW));

    // switch pull-ups
    SW_FUEL_PORT |= _BV(SW_FUEL);
    SW_START_PORT |= _BV(SW_START);
    //SW_PACECAR_PORT |= _BV(SW_PACECAR);
    SLOT1_PORT &= ~_BV(SLOT1);
    SLOT2_PORT &= ~_BV(SLOT2);

    // control outputs
//    DDR(LAP_COUNTER_PORT) |= _BV(LAP_COUNTER);

    // setup rail control
    //RAIL_DETECT_PORT |= _BV(RAIL_DETECT); // enable internal pull-up
//    DDR(RAIL_POWER_PORT) |= _BV(RAIL_POWER);

    // setup debut output on i2c pins
    DDR(PORTC) |= _BV(PC0) | _BV(PC1);

    // display init sequence on LEDs
    LED(1, 1); blinkdelay();
    LED(2, 1); blinkdelay();
    LED(3, 1); blinkdelay();
    LED(4, 1); blinkdelay();
    LED(5, 1); blinkdelay();
    LED(1, 0); blinkdelay();
    LED(2, 0); blinkdelay();
    LED(3, 0); blinkdelay();
    LED(4, 0); blinkdelay();
    LED(5, 0);


    // setup response receiver timer
//    TCCR0 = (1<<CS01); //divide by 8
    // interrupt enable + tcnt0 set in timer2


    // setup data bit + carid timer
//    TCCR2 = (1<<CS21) | (1<<WGM21); //divide by 8, set compare match
//    OCR2 = TIMER2_50US;
//    TIMSK |= _BV(OCIE2); //enable timer2 interrupt

    // enable carid interrupts
    MCUCR = _BV(ISC00) | _BV(ISC01) | _BV(ISC10) | _BV(ISC11); // INT0/1 rising edge
    GICR = _BV(INT0) | _BV(INT1) ; // Enable INT0 + INT1



    // setup data packet timer
    TCCR1B = (1<<CS11) | (1<<WGM12); //divide by 8, set compare match (1mhz clock)
    OCR1A = TIMER1_500NS;
    //OCR1A = TIMER1_7500NS;
    TIMSK |= _BV(OCIE1A); //enable timer1 interrupt

    RS232_puts_p(PSTR("FreeSlot Blackbox v"));
    RS232_puts(VERSION);
    RS232_putc('\n');

}

void panic_mode(void) {
//    RAIL_POWER_PORT &= ~_BV(RAIL_POWER); // disable rails power
	lcd_printlc_P(1, 1, PSTR("Panic mode          "));
    LEDS_OFF();
    LED(1, 2);
    blinkdelay();
    LED(1, 2);
    LED(2, 2);
    blinkdelay();
    LED(2, 2);
    LED(3, 2);
    blinkdelay();
    LED(3, 2);
    LED(4, 2);
    blinkdelay();
    LED(4, 2);
    LED(5, 2);
    btn_p = (PIN(SW_PACECAR_PORT) & _BV(SW_PACECAR));
     if (old_p != btn_p) {
         // pacecar button changed
         if (btn_p == 0) pacecarbutton();
         old_p = btn_p;
     }
}


//uint8_t check_rails_shortcut(void) {
//    // check for short circuit on the rails
//    uint8_t i = 100;
//    if ((PIN(RAIL_DETECT_PORT) & _BV(RAIL_DETECT)) == 0) {
//        while (i>0) {
//            if ((PIN(RAIL_DETECT_PORT) & _BV(RAIL_DETECT)) != 0) return 0;
//            _delay_us(20);
//            i--;
//        }
//        if ((PIN(RAIL_DETECT_PORT) & _BV(RAIL_DETECT)) == 0) {
//            // set panic mode
//            mode = 0xff;
//            RAIL_POWER_PORT &= ~_BV(RAIL_POWER); // disable rails power
//            // wait a little to all interrupts complete
//            //cli(); // disable ALL Interrupts
//            _delay_ms(2);
//            RAIL_POWER_PORT &= ~_BV(RAIL_POWER); // disable rails power
//            RS232_puts_p(SHORTCUT);
//            LED(1, 1);
//            LED(2, 1);
//            LED(3, 0);
//            LED(4, 0);
//            LED(5, 0);
//            while (mode == 0xff) {
//                LED(1, 2);
//                LED(2, 2);
//                LED(4, 2);
//                LED(5, 2);
//                blinkdelay();
//                LED(3, 2);
//                blinkdelay();
//                LED(3, 2);
//                blinkdelay();
//                LED(3, 2);
//                blinkdelay();
//                LED(3, 2);
//                btn_start = (PIN(SW_START_PORT) & _BV(SW_START));
//                if (old_start != btn_start) {
//                    // start button changed
//                    if (btn_start == 0) {
//                        mode = 0;
//                        RS232_puts_p(RESUME);
//                    }
//                    old_start = btn_start;
//                }
//
//            }
//            LEDS_OFF();
//            LED(3, 1);
//        }
//    }
//    return 0;
//}
