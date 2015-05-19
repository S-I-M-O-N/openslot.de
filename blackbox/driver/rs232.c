#include <util/setbaud.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdlib.h>
#include "../main.h"
#include "rs232.h"

void RS232_init(void) {
    // first init UART
    // set baud rate
    UBRRH = UBRRH_VALUE;
    UBRRL = UBRRL_VALUE;
    #if USE_2X
        UCSRA |= (1 << U2X);
    #else
        UCSRA &= ~(1 << U2X);
    #endif
    // enable receiver and transmitter and Rx interrupts
    UCSRB = (1<<RXEN) | (1<<TXEN)  | (1 << RXCIE);
    // framing format 8N1
    UCSRC = (1<<URSEL) | (1<<UCSZ1) | (1<<UCSZ0);
    // enable interrupts
    sei();
}

static void RS232_txc(void) {
    while (!(UCSRA & (1<<TXC))); // wait for TXC
    UCSRA &= ~(1<<TXC); // clear TXC
}

void RS232_putc(char c) {
    // send char
    loop_until_bit_is_set(UCSRA, UDRE);
    UDR = c;
    //loop_until_bit_is_set(UCSRA, UDRE);
    //RS232_txc();
}

void RS232_puts(char* s) {
    while (*s != 0) {
        // send char
        loop_until_bit_is_set(UCSRA, UDRE);
        UDR = *s;
        s++;
    }
    //loop_until_bit_is_set(UCSRA, UDRE);
    //RS232_txc();
}

void RS232_puts_p(const char* s) {
    char c=pgm_read_byte(s);
    while (c != 0) {
        // send char
        loop_until_bit_is_set(UCSRA, UDRE);
        UDR = c;
        c=pgm_read_byte(++s);
    }

    //loop_until_bit_is_set(UCSRA, UDRE);
    //RS232_txc();
}


