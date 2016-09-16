#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "driver/rs232.h"
#include "driver/adc.h"

#include "driver/i2clcd.h"
#include "driver/i2cmaster.h"

#include "main.h"
#include "lowlevel.h"

const char ok[]               PROGMEM="OK\n";
const char busy[]             PROGMEM="BUSY\n";
const char prepare[]          PROGMEM="!RACE PREPARE\n";
const char countdownstart[]   PROGMEM="!COUNTDOWN\n";
const char racestart[]        PROGMEM="!RACE START\n";

const char SHORTCUT[]         PROGMEM="!SHORTCUT\n";
const char PANIC[]            PROGMEM="!PANIC\n";
const char RESUME[]           PROGMEM="!RESUME\n";

typedef union {
    uint32_t value;
    uint16_t word[2]; // high, low word
    uint8_t  byte[4]; // all four bytes
} u32;

//#define FUEL_FULL       10000
//#define FUEL_JUMPSTART  5000
//#define FUEL_DIV_DEFAULT 25
//#define PITLANE_SPEEDLIMIT 10
#define FINISHLINE_DIFF_BLOCK 1500 // minimum 750 ms between laps (prevents double counted lapt on finish crossing)
//volatile uint8_t fuel_divisor = FUEL_DIV_DEFAULT; // default, can be set via serial command

typedef struct {
    unsigned speedlimit:4; // 4bits speedlimit
    unsigned speedminimum:4; // 4bits speedminimum
    unsigned seccnt:4; // 4 bits tenth seconds counter
    unsigned accel:4; // 4 bits last configured acceleration
    unsigned speed:4; // 4 bits last speed
    unsigned pitlane_limit:4; // 4bits speedlimit
    unsigned trackswitch:1; // 1bit bool
    unsigned canrefuel:1; // 1bit bool
    unsigned unlimitedfuel:1; // 1bit bool
    unsigned trackchange:1; // 1bit bool
    uint16_t jumpstart_time, fuel;
    uint8_t laps, position;
    u32 lap_time_start, lap_time, best_time;
} cardata;

static char s[8];
static uint8_t  countdown, countdown_loops;
extern uint8_t  mode = 0;
// valid race modes:
// 0: free drive / idle
// 1: waiting for countdown start
// 2: race countdown initiated
// 3: Race start condition
// 255 PANIC MODE -> TURN OFF TRACK POWER

extern uint8_t  btn_start = _BV(SW_START);
extern uint8_t  old_start = _BV(SW_START);
extern uint8_t  btn_pacecar = _BV(SW_PACECAR);
extern uint8_t  old_pacecar = _BV(SW_PACECAR);

// todo: pack as bit structure:

typedef struct {
    unsigned fuel_enabled:1;
    unsigned pitlane_finishline:1;
    unsigned liveinfo:6;
    unsigned lap_limit:1;
    unsigned time_limit:1;
} switches_s;
switches_s switches;

volatile u32 sysclk;
volatile uint8_t sysclk_packettimer = 0;
volatile cardata slot[MAX_SLOTS];

volatile uint8_t car0, car1;
volatile uint16_t car0_new, car0_old;
volatile uint16_t car1_new, car1_old;
uint8_t car0_state, car1_state;

volatile uint8_t program_count = 0;
volatile uint8_t program_id;
volatile uint8_t program_command;
volatile uint8_t program_parameter;

volatile uint8_t datalen = 0;
char data[10]; // 8 bytes data buffer + string termination

static char buffer[RS232_BUFSIZE+1];
static uint8_t buffer_len;

volatile uint16_t transmit_buffer;
volatile uint16_t transmit_buffer_queue;
volatile uint8_t  transmit_len;
volatile uint8_t  transmit_len_next;
volatile uint8_t  transmit_len_queue;

volatile uint16_t responsewire_data = 0;
volatile uint16_t response;
volatile uint8_t response_len;
volatile uint8_t timer0_delay;

volatile uint16_t halfmilisecs = 0;
volatile uint16_t decisecs = 0;

volatile uint8_t lap_limit = 10;
volatile uint32_t time_limit = 180000; // 180000 =  90 seconds = 90 * 2000 * 500ns

volatile uint16_t beep = 0;


    /* RESPONSEWIRE frame format:
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

    /* PITLANE/TRACKSWITCH STATUS DEFINITION
        1 = AA
        2 = AB
        3 = BB
        4 = BA
        5 = BC
        6 = ZZ -> pitlane exit
    */

//#define STARTSTOP 0b1000000000000001
//void decode_responsewire(void) {
//    // ATTENTION: THIS IS CALLED IN AN INTERRUPT, KEEP AS SHORT AS POSSIBLE
//
//    uint16_t data = responsewire_data;
//    // first check if start + stopbit are set
//    // todo future: unsure but last bit doesnt get set?!
///*
//    if ((data & STARTSTOP) != STARTSTOP) {
//        RS232_puts("RW BAD\n");
//        return; // incomplete response
//    }
//*/
//    // now extract the car id, track change status
//    data >>= 1;
//    uint8_t car = (data & 0b111);
//    data >>= 3;
//    uint8_t status = (data & 0b1111);
//    data >>= 4;
//    uint8_t sender = (data & 0b1111);
//    data >>= 4;
//    uint8_t type = (data & 0b111);
//    if (type == 4) {
//        // pitlane response
//        if (status == 5) {
//            slot[car].canrefuel = 1;
//            if (switches.pitlane_finishline) slot[car].laps++;
//        }
//        if (status == 6) for (data=0; data<MAX_SLOTS; data++) slot[data].canrefuel = 0;
//        if (status == 7) slot[car].canrefuel = 0;
//    }
//    RS232_puts("RW:");
//    RS232_putc(car + '0');
//    RS232_putc(':');
//    RS232_putc(type + '0');
//    RS232_putc(':');
//    itoa(sender, s, 16);
//    RS232_puts(s);
//    RS232_putc(':');
//    itoa(status, s, 16);
//    RS232_puts(s);
//    RS232_putc(':');
//    ultoa(sysclk.value, s, 16);
//    RS232_puts(s);
//    RS232_putc('\n');
//}

//int insert_queue(uint16_t tmp, uint8_t len) {
//    if (transmit_buffer_queue == 0) {
//        transmit_buffer_queue = tmp;
//        transmit_len_queue = len;
//        return 1;
//    }
//    return 0;
//}

void startbutton(void) {
    // start button press active
    if (mode == 0) {
        mode = 1; // set wait for race start mode
        reset_vars();
        LED(1, 1);
        LED(2, 1);
        LED(3, 1);
        LED(4, 1);
        LED(5, 1);
        RS232_puts_p(prepare);
        lcd_printlc_P(1, 1, PSTR("Race prepare        "));
        lcd_printlc_P(2, 1, PSTR("T P L   last  best S"));
        lcd_printlc_P(3, 1, PSTR("1 0 0   0.00  0.00  "));
        lcd_printlc_P(4, 1, PSTR("2 0 0   0.00  0.00  "));
        if((PIN(SW_FUEL_PORT) & _BV(SW_FUEL)) == 1){
        	switches.lap_limit = 0;
        	switches.time_limit = 1;
        }
        else {
        	switches.lap_limit = 1;
        	switches.time_limit = 0;
        }

    } else if (mode == 1) {
        // Initiate race countdown
        sysclk.value = 0;
        countdown = 5;
        countdown_loops = COUNTDOWN_DELAY;
        mode = 2;
        LED(1, 0);
        LED(2, 0);
        LED(3, 0);
        LED(4, 0);
        LED(5, 0);
        RS232_puts_p(countdownstart);
        lcd_printlc_P(1, 1, PSTR("Race countdown      "));
    }
}

void pacecarbutton(void) {
    // pacecar button press active -> we will use it to trigger panic mode
     if (mode != 0xff) {
                        mode = 0xff;
                        RS232_puts_p(PANIC);
                    }
     else {
                        mode = 0;
                        LEDS_OFF();
                        RS232_puts_p(RESUME);
                        lcd_printlc_P(1, 1, PSTR("Resume              "));
        }
}

ISR ( USART_RXC_vect ) {
    uint8_t tmp;
    char c = UDR;

    // check for buffer overflow
    if (buffer_len==sizeof(buffer)) {
        buffer_len=0;
    }  else {
        // collect characters until end of line
        if ( (c=='\n') ) {
            buffer[buffer_len]=0;

            // packet end received, parse the received packet
            switch (buffer[0]) {
//                case 'P': // inject a program data word to the rails
//                    if (program_count == 0) {
//                        program_id = buffer[3]-'0';
//                        program_command = buffer[1]-'0';
//                        if (program_command > 9)
//                            program_command = buffer[1]-'A'+10;
//                        program_parameter = buffer[2]-'0';
//                        if (program_parameter > 9)
//                            program_parameter = buffer[2]-'A'+10;
//                        if (program_command < 4)
//                            program_count = 0x02; // send commands twice (fuel, speed, brake)
//                            else
//                            program_count = 0x01;
//                        RS232_puts_p(ok);
//                    } else RS232_puts_p(busy);
//                    break;

//                case 'L': // Limit maximum speed for a car
//                    tmp = buffer[2]-'0';
//                    if (tmp > 9)
//                        tmp = buffer[2]-'A'+10;
//                    slot[buffer[1]-'0'].speedlimit = tmp;
//                    RS232_puts_p(ok);
//                    break;
//
//                case 'S': // set minimum speed for a car
//                    tmp = buffer[2]-'0';
//                    if (tmp > 9)
//                        tmp = buffer[2]-'A'+10;
//                    slot[buffer[1]-'0'].speedminimum = tmp;
//                    RS232_puts_p(ok);
//                    break;
//
//                case 'F': // set fuel enabled
//                    switches.fuel_enabled = buffer[1]-'0';
//                    if (buffer[2] == ':') {
//                        // convert fuel divisor
//                        buffer[0] = '0'; // atoi expects zeros!
//                        buffer[1] = '0';
//                        buffer[2] = '0';
//                        RS232_puts("DivisorChange: ");
//                        itoa(fuel_divisor, s, 10);
//                        RS232_puts(s);
//                        RS232_putc('>');
//                        fuel_divisor = atoi(buffer);
//                        itoa(fuel_divisor, s, 10);
//                        RS232_puts(s);
//                        RS232_putc(' ');
//                    }
//                    RS232_puts_p(ok);
//                    break;

//                case '*': // set live information
//                    switches.liveinfo = buffer[1]-'0';
//                    RS232_puts_p(ok);
//                    break;

//                case 'X': // set Pitlane act as finish line
//                    // this can be used if pitlane exit comes after finish line while pitlane entry is before finish line
//                    switches.pitlane_finishline = buffer[1]-'0';
//                    RS232_puts_p(ok);
//                    break;


                case '#': // remote start button press
                    RS232_puts_p(ok);
                    startbutton();
                    break;

                case '+': // toggle panic mode
                    RS232_puts_p(ok);
                    if (mode != 0xff) {
                        mode = 0xff; 
                        RS232_puts_p(PANIC);
                    } else {
                        mode = 0;
                        RS232_puts_p(RESUME);
                        lcd_printlc_P(1, 1, PSTR("Resume              "));
                    }
                    break;

//                case 'I': // get Information data (incl. important global parameter dump)
//                    RS232_puts(VERSION);
//                    RS232_putc(':');
//                    for (tmp=0;tmp<MAX_SLOTS;tmp++) RS232_putc(slot[tmp].speedlimit); // output speed limits
//                    RS232_putc(':');
//                    for (tmp=0;tmp<MAX_SLOTS;tmp++) RS232_putc(slot[tmp].speedminimum); // output minimum speed
//                    RS232_putc(':');
//                    for (tmp=0;tmp<MAX_SLOTS;tmp++) {
//                        itoa(slot[tmp].fuel, s, 16);
//                        RS232_putc(s); // output fuel levels (0=empty, 100=full, 0xff=no fuel option)
//                        RS232_putc(',');
//                    }
//                    RS232_putc(':');
//                    for (tmp=0;tmp<MAX_SLOTS;tmp++) {
//                        itoa(slot[tmp].jumpstart_time, s, 16);
//                        RS232_puts(s); // output jumpstart times
//                        RS232_putc(',');
//                    }
//                    RS232_putc(':');
//                    RS232_putc('\n');
//                    break;

            }

            // wait for the next packet
            buffer_len=0;
        } else {
            buffer[buffer_len++]=c;
        }
    }
}


//void jumpstart(uint8_t controller) {
//    if (slot[controller].jumpstart_time == 0) {
//        slot[controller].jumpstart_time = sysclk.word[0]; // low word
//        RS232_putc('~');
//        RS232_putc('0'+controller);
//        RS232_putc(':');
//        itoa(slot[controller].jumpstart_time, s, 16);
//        RS232_puts(s);
//        RS232_putc('\n');
//        slot[controller].fuel = FUEL_JUMPSTART; // suck 50% off that bitch!
//    }

//}

//uint8_t do_controller(uint8_t controller) {
//    // read controller X speed & encode controller data packet
//    uint16_t tmp = 0;
//    uint8_t speed;
//    uint8_t trackchange = 0xff;
//
//    if ( (PIN(SW_PACECAR_PORT) & _BV(SW_PACECAR)) == 0 ) {
//        // map controller 1+2 to 5+6
//        if (controller == 4) tmp = ((getADC(CONTROLLER1_SPEED) / CONTROLLER_DIVISOR) & 0x0F);
//        if (controller == 5) tmp = ((getADC(CONTROLLER2_SPEED) / CONTROLLER_DIVISOR) & 0x0F);
//        if (controller == 4) trackchange = (PIN(CONTROLLER_PORT) & _BV(CONTROLLER1_SW));
//        if (controller == 5) trackchange = (PIN(CONTROLLER_PORT) & _BV(CONTROLLER2_SW));
//    } else {
//        // read speeds
//        if ((controller == 0) && (mode!=1)) tmp = ((getADC(CONTROLLER1_SPEED) / CONTROLLER_DIVISOR) & 0x0F);
//        if ((controller == 1) && (mode!=1)) tmp = ((getADC(CONTROLLER2_SPEED) / CONTROLLER_DIVISOR) & 0x0F);
//        if ((controller == 2) && (mode!=1)) tmp = ((getADC(CONTROLLER3_SPEED) / CONTROLLER_DIVISOR) & 0x0F);
//        if ((controller == 3) && (mode!=1)) tmp = ((getADC(CONTROLLER4_SPEED) / CONTROLLER_DIVISOR) & 0x0F);
//        if (controller == 0) trackchange = (PIN(CONTROLLER_PORT) & _BV(CONTROLLER1_SW));
//        if (controller == 1) trackchange = (PIN(CONTROLLER_PORT) & _BV(CONTROLLER2_SW));
//        if (controller == 2) trackchange = (PIN(CONTROLLER_PORT) & _BV(CONTROLLER3_SW));
//        if (controller == 3) trackchange = (PIN(CONTROLLER_PORT) & _BV(CONTROLLER4_SW));
//
//        if (controller < 4) {
//            if (tmp < slot[controller].speedminimum) tmp = slot[controller].speedminimum;
//            if ((mode == 2) && (tmp != 0)) { jumpstart(controller); tmp = 0; }
//            if (tmp > slot[controller].speedlimit) tmp = slot[controller].speedlimit;
//            if (slot[controller].canrefuel && (tmp > slot[controller].pitlane_limit)) tmp = slot[controller].pitlane_limit;
//        } else {
//            if ((mode == 0) && (tmp < slot[controller].speedminimum)) tmp = slot[controller].speedminimum;
//            if (tmp > slot[controller].speedlimit) tmp = slot[controller].speedlimit;
//            // //if ( (trackchange!=0) || (slot[controller].trackswitch)) tmp |= (1<<5);
//        }
//    }
//    speed = tmp;
//    slot[controller].speed = tmp;
//    slot[controller].trackchange = (trackchange != 0);
//    tmp = tmp << 1;
//    if (trackchange != 0) tmp |= (1<<5);
//
//
//    switch (controller) {
//        case 0:
//            if (trackchange != 0) {
//                if (mode == 0) LED(1,0);
//            } else if (mode == 0) LED(1,1);
//            break;
//        case 1:
//            if (trackchange != 0) {
//                if (mode == 0) LED(2,0);
//            } else if (mode == 0) LED(2,1);
//            break;
//        case 2:
//            if (trackchange != 0) {
//                if (mode == 0) LED(4,0);
//            } else if (mode == 0) LED(4,1);
//            break;
//        case 3:
//            if (trackchange != 0) {
//                if (mode == 0) LED(5,0);
//            } else if (mode == 0) LED(5,1);
//            break;
//    }
//
//    tmp |=  (0b1000000000 | (controller << 6));
//    // FUEL BIT GETS SET WHEN FUEL == 0,
//    // THIS REQUIRES PHYSICAL CAR FUEL LEVEL SET TO ZERO BEFORE!
//    if ( ((PIN(SW_FUEL_PORT) & _BV(SW_FUEL)) != 0) | (slot[controller].fuel == 0)) tmp |= 1;
//    if (insert_queue(tmp, 9)) {
//        if ((switches.fuel_enabled) && (slot[controller].unlimitedfuel == 0)) {
//            if ((speed != 0) && (fuel_divisor > 0)) {
//                // do the fuel calculation, regardless if fuel logic active or not
//                tmp = (uint8_t)(((slot[controller].accel * speed) + 1) / fuel_divisor);
//                if (tmp == 0) tmp = 1;
//                if (slot[controller].fuel > 0) {
//                    // enough fuel left to decrement?
//                    if (slot[controller].fuel > tmp) {
//                        slot[controller].fuel -= tmp; // decrement fuel level
//                    } else slot[controller].fuel = 0;
//                }
//            } else if (slot[controller].canrefuel) {
//                // increase fuel by 5%/sec, this equals by adding 50 to the counter
//                slot[controller].fuel += 50;
//                if (slot[controller].fuel > FUEL_FULL) {
//                    slot[controller].fuel = FUEL_FULL;
//                    slot[controller].canrefuel = 0;
//                }
//            }
//            return 1;
//        }
//        return 1;
//    } else return 0;
//}

//uint8_t mirror( uint8_t n ) {
//    n = ((n >> 1) & 0x55) | ((n << 1) & 0xaa);
//    n = ((n >> 2) & 0x33) | ((n << 2) & 0xcc);
//    n = ((n >> 4) & 0x0f) | ((n << 4) & 0xf0);
//    return n;
//}

//int do_program(uint8_t controller, uint8_t command, uint8_t parameter) {
//    // send program data packet
//    uint16_t tmp;
//    parameter = mirror(parameter);
//    controller = mirror(controller);
//    command = mirror(command);
//    tmp = 0b1000000000000 | (parameter << 4) | command | (controller >> 5);
//    return insert_queue(tmp, 12);
//}

//int do_active(void) {
//    // send controller active data packet
//    uint16_t tmp = 0b10000000;
//    if ( (PIN(SW_PACECAR_PORT) & _BV(SW_PACECAR)) == 0 ) {
//        // map controller 1+2 to 5+6
//        if ((getADC(CONTROLLER1_SPEED) / CONTROLLER_DIVISOR) > 0) tmp |= 0b10000101;
//        if ((getADC(CONTROLLER2_SPEED) / CONTROLLER_DIVISOR) > 0) tmp |= 0b10000011;
//    } else {
//        if ((slot[0].speedminimum != 0) || ((getADC(CONTROLLER1_SPEED) / CONTROLLER_DIVISOR) > 0)) tmp |= 0b11000001;
//        if ((slot[1].speedminimum != 0) || ((getADC(CONTROLLER2_SPEED) / CONTROLLER_DIVISOR) > 0)) tmp |= 0b10100001;
//        if ((slot[2].speedminimum != 0) || ((getADC(CONTROLLER3_SPEED) / CONTROLLER_DIVISOR) > 0)) tmp |= 0b10010001;
//        if ((slot[3].speedminimum != 0) || ((getADC(CONTROLLER4_SPEED) / CONTROLLER_DIVISOR) > 0)) tmp |= 0b10001001;
//        if (slot[4].speedminimum != 0) tmp |= 0b10000101;
//        if (slot[5].speedminimum != 0) tmp |= 0b10000011;
//    }
//    // todo: wenn Daten enpfangen wurden hier eine Quittierung senden anstatt dem Active Word
//
//    return insert_queue(tmp, 7);
//}

//int do_pace_ghost(void) {
//    // send ghost and pacecar data packet
//    // todo: at the moment, both disabled!
//    uint16_t tmp = 0b1111100000;
//    if ( (PIN(SW_FUEL_PORT) & _BV(SW_FUEL)) != 0) tmp |= 1; // benzinstand aktiv - tankmodusschalter
//    // todo: PC, NH, TK, (KFR, FR)
//
//    return insert_queue(tmp, 9);
//}

#include "interrupts.c"

void reset_vars(void) {
    uint8_t i;
    for (i=0; i<MAX_SLOTS; i++) {
//        slot[i].speedlimit = 15;
        //slot[i].speedminimum = 0; // issue #688
//        slot[i].trackswitch = 0;
//        slot[i].fuel = FUEL_FULL;
//        slot[i].jumpstart_time = 0;
        slot[i].laps = 0;
        slot[i].seccnt = 0;
//        slot[i].accel = 15; // full acceleration per default - TODO
//        slot[i].canrefuel = 0;
//        slot[i].pitlane_limit = PITLANE_SPEEDLIMIT;
//        if (i<4) slot[i].unlimitedfuel = 0; else slot[i].unlimitedfuel = 1;
        slot[i].lap_time_start.value = 0;
        slot[i].lap_time.value = 0;
        slot[i].best_time.value = 0;
        slot[i].position = 0;
    }
    sysclk.value = 0;
}

void countdown_progress(void) {
    // decrement COUNTDOWN_LOOPS
    if (countdown_loops>0) {
        countdown_loops--;
    } else {
        countdown--;
        countdown_loops = COUNTDOWN_DELAY;
    }
    switch (countdown) {
        case 5: LED(1, 1); break;
        case 4: LED(2, 1); break;
        case 3: LED(3, 1); break;
        case 2: LED(4, 1); break;
        case 1: LED(5, 1); break;
        case 0: { // RACE START!
            sysclk.value = 0;
            LEDS_OFF();
//            LED(1, 0);
//            LED(2, 0);
//            LED(3, 1);
//            LED(4, 0);
//            LED(5, 0);
            mode = 3;
            lcd_printlc_P(1, 1, PSTR("Race start          "));
            } break;
    }
}

void lapinfo0(void) {
						RS232_puts("L:3:"); // 3 = BB
	                    itoa(slot[0].laps, s, 16);
	                    RS232_puts(s);
	                    RS232_putc(':');
	                    RS232_putc('1');
	                    RS232_putc(':');
	                    ultoa(slot[0].lap_time.value, s, 16);
	                    RS232_puts(s);
	                    RS232_putc(':');
	                    ultoa(sysclk.value, s, 16);
	                    RS232_puts(s);
	                    RS232_putc('\n');

	                    lcd_gotolc(3,3);
	                    itoa(slot[0].position, s, 10);  //Positon Slot 1
	                    lcd_print(s);
	                    lcd_gotolc(4,3);
	                    itoa(slot[1].position, s, 10);  //Position Slot 2
	                    lcd_print(s);

	                    lcd_gotolc(3,5);
	                    itoa(slot[0].laps, s, 10);
	                    lcd_print(s);
	                    lcd_gotolc(3,8);
	                    dtostrf(slot[0].lap_time.value/2000.00, 5, 2, s);
	                    lcd_print(s);
	                    lcd_gotolc(3,13);
	                    lcd_print(" ");
	                    lcd_gotolc(3,14);
	                    dtostrf(slot[0].best_time.value/2000.00, 5, 2, s);
	                    lcd_print(s);
	                    lcd_gotolc(3,19);
	                    lcd_print("  ");
}

void lapinfo1(void) {
	                    RS232_puts("L:1:"); // 1 = AA
	                    itoa(slot[1].laps, s, 16);
	                    RS232_puts(s);
	                    RS232_putc(':');
	                    RS232_putc('2');
	                    RS232_putc(':');
	                    ultoa(slot[1].lap_time.value, s, 16);
	                    RS232_puts(s);
	                    RS232_putc(':');
	                    ultoa(sysclk.value, s, 16);
	                    RS232_puts(s);
	                    RS232_putc('\n');

	                    lcd_gotolc(3,3);
	                    itoa(slot[0].position, s, 10);  //Position Slot 1
	                    lcd_print(s);
	                    lcd_gotolc(4,3);
	                    itoa(slot[1].position, s, 10);  //Position Slot 2
	                    lcd_print(s);

	                    lcd_gotolc(4,5);
	                    itoa(slot[1].laps, s, 10);
	                    lcd_print(s);
	                    lcd_gotolc(4,8);
	                    dtostrf(slot[1].lap_time.value/2000.00, 5, 2, s);
	                    lcd_print(s);
	                    lcd_gotolc(4,13);
	                    lcd_print(" ");
	                    lcd_gotolc(4,14);
	                    dtostrf(slot[1].best_time.value/2000.00, 5, 2, s);
	                    lcd_print(s);
	                    lcd_gotolc(4,19);
	                    lcd_print("  ");
}

void check_cars(void) {
    u32 clk;
    clk.value = sysclk.value; // freeze system clock time
    if (_BV(INT0) != 0 && slot[0].lap_time_start.value < clk.value + FINISHLINE_DIFF_BLOCK) {
       	GICR |= _BV(INT0); //enable INT0 interrupt
    }
    if (_BV(INT1) != 0 && slot[1].lap_time_start.value < clk.value + FINISHLINE_DIFF_BLOCK) {
       	GICR |= _BV(INT1); //enable INT1 interrupt
    }
//    if (car0 != car0_state) {
//        car0_state = car0;
//        if (car0_state != 0) {
//            diff.value = clk.value - slot[car0-1].lap_time_start.value;
//            if ( diff.value > FINISHLINE_DIFF_BLOCK ) {
//                 if (slot[car0-1].lap_time_start.value != 0) {
//                    slot[car0-1].lap_time.value = diff.value;
//                    slot[car0-1].laps++;
//                    RS232_puts("L:3:"); // 3 = BB
//                    itoa(slot[car0-1].laps, s, 16);
//                    RS232_puts(s);
//                    RS232_putc(':');
//                    RS232_putc('1');
//                    RS232_putc(':');
//                    ultoa(diff.value, s, 16);
//                    RS232_puts(s);
//                    RS232_putc(':');
//                    ultoa(clk.value, s, 16);
//                    RS232_puts(s);
//                    RS232_putc('\n');
//                }
//                slot[car0-1].lap_time_start.value = clk.value;
//            }
//        }
//    } car0 = 0;

//
//    if (car1 != car1_state) {
//        car1_state = car1;
//        if (car1_state != 0) {
//            diff.value = clk.value - slot[car1-1].lap_time_start.value;
//            if ( diff.value > FINISHLINE_DIFF_BLOCK ) {
//                if (slot[car1-1].lap_time_start.value != 0) {
//                    slot[car1-1].lap_time.value = diff.value;
//                    slot[car1-1].laps++;
//                    RS232_puts("L:1:"); // 1 = AA
//                    itoa(slot[car1-1].laps, s, 16);
//                    RS232_puts(s);
//                    RS232_putc(':');
//                    RS232_putc('2');
//                    RS232_putc(':');
//                    ultoa(diff.value, s, 16);
//                    RS232_puts(s);
//                    RS232_putc(':');
//                    ultoa(clk.value, s, 16);
//                    RS232_puts(s);
//                    RS232_putc('\n');
//                }
//                slot[car1-1].lap_time_start.value = clk.value;
//            }
//        }
//    } car1 = 0;

}

//void slot_liveinfo(uint8_t idx) {
//    if (switches.liveinfo == 0) return;
//
//    if (switches.liveinfo == 1) {
//        // increment packet counter, if == 10 output some live info
//        if (slot[idx].seccnt == 10) {
//            // output current fuel status
//            RS232_putc('F');
//            RS232_putc(':');
//            RS232_putc(idx + '0');
//            RS232_putc(':');
//            itoa(slot[idx].fuel, s, 16);
//            RS232_puts(s);
//            RS232_putc(':');
//            ultoa(sysclk.value, s, 16);
//            RS232_puts(s);
//            RS232_putc('\n');
//            slot[idx].seccnt = 0;
//        } else slot[idx].seccnt++;
//    } else if ( (switches.liveinfo - 2) == idx ) {
//            // output controller status LIVEINFO-2 for remote learning
//            RS232_putc('L');
//            RS232_putc('N');
//            RS232_putc(':');
//            itoa(slot[idx].speed, s, 16);
//            RS232_puts(s);
//            RS232_putc(':');
//            if (slot[idx].trackchange)
//            RS232_putc('X');
//            RS232_putc('\n');
//    }
//}


int main(void)
{
    uint8_t  packet_index = 1;

    switches.fuel_enabled = 1;
    switches.pitlane_finishline = 0;
    switches.liveinfo = 0;
    switches.lap_limit = 0;
    switches.time_limit = 0;

    init_hardware();
    reset_vars();
    LED(3, 1); // enable middle led == idle mode
    lcd_command(LCD_CLEAR);
    _delay_ms(2);
    lcd_printlc_P(1, 1, PSTR("Lapcounter ready    "));
    lcd_printlc_P(3, 1, PSTR("Laplimit [laps]: "));
    itoa(lap_limit, s, 10);
    lcd_print(s);
    lcd_printlc_P(4, 1, PSTR("Time limit [s]:"));
    dtostrf(time_limit/2000.00, 3, 0, s);
    lcd_print(s);



    // switch on rails power
//    RAIL_POWER_PORT |= _BV(RAIL_POWER);

    while (1) {
        // check for short circuit on the rails
//        check_rails_shortcut();
    	//lcd_gotolc(1,1);
    //	dtostrf(sysclk.value/2000.00, 5, 2, s);
   // 	lcd_print(s);
        if (sysclk.value >= time_limit && switches.time_limit == 1) {
        	if (slot[0].position == 1) {
        		lcd_printlc_P(3, 20, PSTR("W"));
        	}
        	else {
        		lcd_printlc_P(4, 20, PSTR("W"));
        	}
        	switches.time_limit = 0;
        	mode = 0xff;
        }
    	check_cars();
        while (mode == 0xff) panic_mode();

        if (mode == 0){


        	if (slot[0].position == 1) {
        		LED(1, 1);
        		LED(5, 0);
        	}
        	if (slot[1].position == 1) {
        		LED(5, 1);
        		LED(1, 0);
        	}
        }
        if (response_len > 0) {
            itoa(response, s, 2);
            response_len = 0;
            //RS232_puts("ANSWER RX: ");
            //RS232_puts(s);
            //RS232_putc('\n');
        }


        // read in button presses
        btn_start = (PIN(SW_START_PORT) & _BV(SW_START));
        if (old_start != btn_start) {
            // start button changed
            if (btn_start == 0) startbutton();
            old_start = btn_start;
        }

        btn_pacecar = (PIN(SW_PACECAR_PORT) & _BV(SW_PACECAR));
         if (old_pacecar != btn_pacecar) {
             // pacecar button changed
             if (btn_pacecar == 0) pacecarbutton();
             old_pacecar = btn_pacecar;
         }

        if (mode==3) {
            // RACE START!
            // issue reset command to lap counter
            mode = 0;
            RS232_puts_p(racestart);
//            program_command = 6;
//            program_parameter = 9;
//            program_id = 0;
//            program_count = 1;
//            LAP_COUNTER_PORT |= _BV(LAP_COUNTER);
            // TODO: beep long
            _delay_us(50);
//            LAP_COUNTER_PORT &= ~_BV(LAP_COUNTER);
        }

        if ((decisecs > 0) && (mode == 2)) {
        	decisecs = 0;
        	countdown_progress();
        }

        if (car0 == 1) {
        	lapinfo0();
        	car0 = 0;
        	//LED(1, 2);
        }

        if (car1 == 1) {
        	lapinfo1();
        	car1 = 0;
        	//LED(2, 2);
        }

//        switch (packet_index) {
//            case 1:
//                if (program_count > 0) {
//                    // command in queue
//                    if (do_program(program_id, program_command, program_parameter)) {
//                        packet_index++;
//                        program_count--;
//                    }
//                } else {
//                    // output idle command
//                    if (do_program(7, 19, 0)) packet_index++; // reset
//                    //if (do_program(7, 20, 15)) packet_index++; // reset / pitstop detect
//                    //if (insert_queue(0, 0)) packet_index++; // null command
//                }
//
//                if ( (packet_index>1) && (mode == 2) ) countdown_progress();
//
//                break;
//            case 2:
//                if (do_pace_ghost()) packet_index++;
//                break;
//            case 3:
//                if (do_active()) packet_index++;
//                break;
//            case 4:
//                if (do_controller(0)) { packet_index++;
//                    slot_liveinfo(0);
//                }
//                break;
//            case 5:
//                if (do_controller(4)) { packet_index++;
//                    slot_liveinfo(4);
//                }
//                break;
//            case 6:
//                if (do_controller(1)) { packet_index++;
//                    slot_liveinfo(1);
//                }
//                break;
//            case 7:
//                if (do_controller(5)) { packet_index++;
//                    slot_liveinfo(5);
//                }
//                break;
//            case 8:
//                if (do_controller(2)) { packet_index++;
//                    slot_liveinfo(2);
//                }
//                break;
//            case 9:
//                if (do_active()) packet_index++;
//                break;
//            case 10:
//                if (do_controller(3)) { packet_index = 1;
//                    slot_liveinfo(3);
//                }
//                // last packet, so reset packet index
//                break;
//        }


    } // main loop end
};

