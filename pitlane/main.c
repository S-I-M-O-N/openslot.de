#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <stdlib.h>
#include <stdint.h>
#include <avr/pgmspace.h>

#include "main.h"

#include "driver/rs232.h"
#include "util/delay.h"


ISR ( USART_RXC_vect ) {
}

#define PULSE_PORT      PORTD
#define PULSE_BIT       PD2
#define RESPONSE_PORT   PORTC
#define RESPONSE_PIN    PC1
#define SOLENOID_A_PORT PORTB
#define SOLENOID_B_PORT PORTB
#define SOLENOID_A_PIN  PB1
#define SOLENOID_B_PIN  PB2

#define TRACKSWITCH_TYPE 4 // 1=double, 2=single left, 3=single right, 4=pitlane
#define TYPE_DOUBLE     1
#define TYPE_SINGLE_L   2
#define TYPE_SINGLE_R   3
#define TYPE_PITLANE    4

#define MAX_SENSORS 3

#define PIT_PORT        PORTC
#define PIT_CONNECT_PIN PC3
#define PIT_GROUND_PIN  PC2 // low active

#define SENS2_PORT      PORTD
#define SENS2_PIN       PD5

#define LED_PORT        PORTB
#define LED_PIN         PB5


// internal analog comparator doesnt work well
//#define ANALOG_COMPARATOR       1

typedef struct {
    unsigned speed:4;
    unsigned trackswitch:1;
    unsigned inside:1;
} cardata;

typedef struct {
    unsigned car:4;
    unsigned state:4;
    uint16_t old, new;
} sensordata;

volatile cardata slot[MAX_SLOTS];
volatile sensordata sens[MAX_SENSORS];

volatile uint16_t data = 0;
volatile uint8_t data_len = 0;
volatile uint8_t bitbuf_len = 0;
volatile uint16_t bitbuf = 0;

volatile uint8_t response = 0;
volatile uint8_t response_car = 0;
uint8_t self_id = 0b1111; // ONLY ONE pitlane

void solenoid_delay(void) {
    _delay_ms(2);
}

void send_response(uint8_t car, uint8_t status) {
    /* frame format:
        1 startbit
        2 car id bit 1
        3 car id bit 2
        4 car id bit 3
        5 track change status bit 1
        6 track change status bit 2
        7 track change status bit 3
        8 track change status bit 4
        9 sender id bit 1
        10 sender id bit 2
        11 sender id bit 3
        12 sender id bit 4
        13 device type bit 1
        14 device type bit 2
        15 device type bit 3
        16 stopbit
    */
    uint16_t data;
    // produce packet
    data = ((car & 0b111) << 1) | ((status & 0b1111) << 4) | ((self_id & 0b1111) << 8) | (TRACKSWITCH_TYPE << 12);
    data |= 0b100000000000001; // make sure start/stop bits are set

    uint8_t index = 16; // bit count maximum
    uint8_t enable = DDR(RESPONSE_PORT) | _BV(RESPONSE_PIN);
    uint8_t disable = DDR(RESPONSE_PORT) & ~_BV(RESPONSE_PIN);
    while (index != 0) {
        if ((data & 1) != 0) {
            DDR(RESPONSE_PORT) = enable; // enable response output
        } else {
            DDR(RESPONSE_PORT) = disable; // disable response output
        }
        data = data >> 1; // next bit prepare
        index--; // decrement index
        _delay_us(49); // bit valid phase
    }
    // finally be sure to release the bus!
    DDR(RESPONSE_PORT) = disable; // disable response output
}


ISR ( INT0_vect ) {
    GICR &= ~_BV(INT0) ; // Disable INT0
    // Startsignal erkannt, ab hier den Timer2 starten,
    // der liest dann alle 50µs den Zustand ein und schreibt das
    // empfangene Bit in den Puffer
    bitbuf = 0; // init
    bitbuf_len = 0b10000000; // init 1 pulse received
    TCNT2 = 0;
    TIMSK |= _BV(OCIE2); //enable timer2 interrupt
}

ISR ( TIMER2_COMP_vect ) {
    uint8_t clock;
    uint8_t state;
    uint8_t state2;
    if ((bitbuf_len & 0b10000000) == 0) clock = 0; else clock = 0xff;
    if ((bitbuf_len & 0b01000000) == 0) state = 0; else state = 0xff;
    if ((PIN(PULSE_PORT) & _BV(PULSE_BIT)) == 0) state2 = 0xff; else state2 = 0;

    if (clock) {
        bitbuf_len &= ~_BV(7); // switch clock to low
        // second pulse of bit
        if ((state==state2) & state2) {
            // two cycles high: packet end received
            data_len = (bitbuf_len & 0b00111111);
            TIMSK &= ~_BV(OCIE2); //disable timer2 interrupt
            GICR |= _BV(INT0) ; // Enable INT0

            //data = bitbuf; // output data
            // write data of controllers to array
            if (data_len == 10) { // controller data packet
                clock = (bitbuf >> 6) & 0b00000111;
                slot[clock].speed = (bitbuf >> 1) & 0x0F;
                slot[clock].trackswitch = (bitbuf >> 5) & 1;
                // current response for this car?
                if (response != 0) {
                    if ( response_car == clock) {
                        // add our ID to response:
                        send_response(clock, response);
                        response = 0;
                    }
                }
            }


        } else {
            bitbuf_len++; // increment bit counter
            bitbuf = bitbuf << 1; // shift bits
            if (state2 == 0) bitbuf |= 1; // receive logic one
        }
    } else {
        bitbuf_len |= _BV(7); // switch clock to high
        // first pulse of bit
        if (state2) {
            bitbuf_len |= _BV(6); // store new state
        } else {
            bitbuf_len &= ~_BV(6); // store new state
        }
    }
}


ISR (TIMER1_OVF_vect) {
    // reset car counters to overflow
    uint8_t i;
    for (i=0;i<MAX_SENSORS;i++)
        sens[i].old = 0xffff;
}

void detect_car(uint8_t idx) {
    uint16_t tmp = 0;
    sens[idx].new = TCNT1; // get current counter
    if (sens[idx].old < sens[idx].new) {
        // calculate difference
        if (sens[idx].car == 0) tmp = sens[idx].new-sens[idx].old;
        if ( (tmp > 54) && (tmp < 74) ) tmp = 1;
        else if ( (tmp > 118) && (tmp < 138) ) tmp = 2;
        else if ( (tmp > 186) && (tmp < 206) ) tmp = 3;
        else if ( (tmp > 246) && (tmp < 266) ) tmp = 4;
        else if ( (tmp > 310) && (tmp < 330) ) tmp = 5;
        else if ( (tmp > 374) && (tmp < 394) ) tmp = 6;
        else tmp = 0;
        sens[idx].car = tmp;
    }
    sens[idx].old = sens[idx].new;
}

ISR (INT1_vect) {
    // car0 detector
    detect_car(0);
}

#ifdef ANALOG_COMPARATOR
ISR (ANA_COMP_vect) {
    // car1 detector
    detect_car(1);
}
#else
// ALTERNATIV:
ISR (TIMER1_CAPT_vect) {
    // car1 detector
    detect_car(1);
}
#endif


int main(void)
{
    uint8_t tmp;

    // setup data bit timer2
    TCCR2 = (1<<CS21) | (1<<WGM21); //divide by 8, set compare match
    OCR2 = TIMER2_50US;

    // initialize timer1 for IR signal detection
#ifdef ANALOG_COMPARATOR
    TCCR1B = _BV(CS01) ; // 1mhz clock
    TIMSK = _BV(OCIE2) | _BV(TOIE1) ; //enable timer1+2
#else
    TCCR1B = _BV(CS01) | _BV(ICNC1) | _BV(ICES1); // 1mhz clock, enable ICP on rising edge
    TIMSK = _BV(OCIE2) | _BV(TOIE1) | _BV(TICIE1); //enable timer1+2 / ICP1
#endif

    // enable both external interrupts
    // int 0 = data RX
    // int 1 = car0 input
    MCUCR = _BV(ISC00) | _BV(ISC01) | _BV(ISC10) | _BV(ISC11); // INT0/1 rising edge
    GICR = _BV(INT0) | _BV(INT1) ; // Enable INT0 + INT1

#ifdef ANALOG_COMPARATOR
    ACSR = _BV(ACIE) | _BV(ACIS1) | _BV(ACIS0); // setup analog comparator
#endif

    // oscillator calibration
    // atmega8@1mhz = 0xac
    // @4mhz = ca 0xa0
    //OSCCAL = 0xa0;
    //OSCCAL = 0x9A;
    //OSCCAL = 0xa0; // internal oscillator @ 4 mhz.... doesnt work accurate!

    RS232_init(); // initialize RS232 interface
    RS232_puts_p(PSTR("Freeslot TrackSwitch / Pitlane v1.4\n"));
    sei();


    DDR(SOLENOID_A_PORT) |= _BV(SOLENOID_A_PIN);
    DDR(SOLENOID_B_PORT) |= _BV(SOLENOID_B_PIN);

    DDR(RESPONSE_PORT) &= ~_BV(RESPONSE_PIN); // switch response off
    RESPONSE_PORT &= ~_BV(RESPONSE_PIN); // switch response off

    // setup pitlane output
    DDR(PIT_PORT) |= _BV(PIT_CONNECT_PIN) | _BV(PIT_GROUND_PIN);

    // setup LED
    DDR(LED_PORT) |= _BV(LED_PIN);
    LED_PORT |= _BV(LED_PIN); // switch LED off

    // CONNECT PITLANE TO MAIN TRACK
    PIT_PORT &= ~_BV(PIT_CONNECT_PIN);
    PIT_PORT |= _BV(PIT_GROUND_PIN);

    while (1) {
        // main loop

    /*
        1 = AA
        2 = AB
        3 = BB
        4 = BA
        5 = BC
        6 = ZZ -> pitlane exit
    */
        if (sens[0].car != sens[0].state) {
            sens[0].state = sens[0].car;
#if (TRACKSWITCH_TYPE != TYPE_PITLANE)
            if ( (sens[0].state != 0) && (slot[sens[0].state-1].trackswitch == 0) && (slot[sens[0].state-1].speed>0) ) {
                response = 2;
                response_car = sens[0].state - 1;

                // set inside status
                slot[sens[0].state].inside = 1;

                // trigger solenoid A
                RS232_putc('A');
                RS232_putc('B');
                RS232_putc('0'+sens[0].state);
                RS232_putc('\n');

                SOLENOID_A_PORT |= _BV(SOLENOID_A_PIN);
                solenoid_delay();
                SOLENOID_A_PORT &= ~_BV(SOLENOID_A_PIN);
                solenoid_delay();
            } else
#endif
            if (sens[0].state != 0) {
                response = 1;
                response_car = sens[0].state - 1;
                RS232_putc('A');
                RS232_putc('A');
                RS232_putc('0'+sens[0].state);
                RS232_putc('\n');
            }
        } sens[0].car = 0;


        if (sens[1].car != sens[1].state) {
            sens[1].state = sens[1].car;
            if ( (sens[1].state != 0) && (slot[sens[1].state-1].trackswitch == 0) && (slot[sens[1].state-1].speed>0) ) {
                response = 5;
                response_car = sens[1].state - 1;

                // set inside status
                slot[sens[1].state-1].inside = 1;

                // trigger solenoid B
                RS232_putc('B');
                RS232_putc('C');
                RS232_putc('0'+sens[1].state);
                RS232_putc('\n');

                SOLENOID_B_PORT |= _BV(SOLENOID_B_PIN);
                solenoid_delay();
                SOLENOID_B_PORT &= ~_BV(SOLENOID_B_PIN);
                solenoid_delay();
            } else
            if (sens[1].state != 0) {
                response = 3;
                response_car = sens[1].state - 1;
                RS232_putc('B');
                RS232_putc('B');
                RS232_putc('0'+sens[1].state);
                RS232_putc('\n');
            }
        } sens[1].car = 0;

        // TODO: At the moment, all "inside" cars gets exit response, but probably only the last response will be sent
        // we have to detect which car is passing sensor2 - but at the moment we are lack of external interrupt source
        if ( (PIN(SENS2_PORT) & _BV(SENS2_PIN)) != 0 ) {
                // set inside status
                for (tmp=0; tmp<MAX_SLOTS; tmp++) {
                    // workaround: only reset tanking on cars which have speed>0, so parking cars will not get "outside"
                    if (slot[tmp].inside && (slot[tmp].speed > 0)) {
                        slot[tmp].inside = 0;
                        response = 7;
                        response_car = tmp;
                    }
                }
                //response = 6;
                //response_car = 0;
                RS232_puts_p(PSTR("PIT:EXIT\n"));
        }

        if (sens[2].car != sens[2].state) {
            sens[2].state = sens[2].car;
            if (sens[2].state != 0) {
                response = 6;
                response_car = sens[2].state-1;

                // set inside status
                slot[sens[2].state-1].inside = 0;

                RS232_putc('Z');
                RS232_putc('Z');
                RS232_putc('0'+sens[2].state);
                RS232_putc('\n');
            }
        } sens[2].car = 0;

        // enable LED when car is in pitlane
        LED_PORT |= _BV(LED_PIN);
        for (tmp=0; tmp<MAX_SLOTS; tmp++)
            if (slot[tmp].inside) LED_PORT &= ~_BV(LED_PIN);


    } // main loop end
};

