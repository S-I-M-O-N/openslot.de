ISR ( TIMER1_COMPA_vect ) {
    if (mode==0xff) return; // panic mode, drop rest of routine
    // trigger packet transfer:
    if (sysclk_packettimer == 14) { // 15*500 = 7500 NS
        transmit_len = transmit_len_next;
        sysclk_packettimer = 0;
    } else sysclk_packettimer++;
    // here is some more time to do something else...


    // reset both car counters to overflow
    car0_old = TIMER1_500NS;
    car1_old = TIMER1_500NS;

    sysclk.value++; // increment 500ns timer
}

ISR ( TIMER2_COMP_vect ) {
    if (mode==0xff) return; // panic mode, drop rest of routine

    uint8_t i;
    //OCR2 = TIMER2_50US; // make sure that timer2 is 50µs !!!
    // data packet timer 100µs pro bit...
    if (transmit_len >= 0xFE) {
        if (transmit_len != 0xFF) {
            RAIL_POWER_PORT |= _BV(RAIL_POWER); // end of transmission
            transmit_len = 0xFF;
            transmit_buffer = transmit_buffer_queue;
            transmit_buffer_queue = 0;
            transmit_len_next = transmit_len_queue;

            // start the response receiver timer
            // TODO: only on 8 timeslots, not on every transmission
            // TODO: give slot number to timer - then store the transmission to 8 slots array
            TCNT0 = TIMER0_250US;
            timer0_delay = TIMER0_2300NS;
            response = 0;
            response_len = 0;
            TIMSK |= _BV(TOIE0);

            // Try to read the stuff on the response wire
            TIMSK &= ~_BV(OCIE2); // temporarily disable timer2 interrupts
            responsewire_data = 0;
            // wait a little and look if wire goes low
            i = 100;
            while ( ((PIN(RESPONSEWIRE_PORT) & _BV(RESPONSEWIRE_PIN)) != 0) && (i>0) ) {
                i--;
                _delay_us(5);
            }
            if (i>0) {
                // response incoming!
                // start feew µs later
                _delay_us(5);
                for (i=16; i>0; i--) { // start receiving all 16 bits -> shift them in same direction as they're shifted out
                    responsewire_data = (responsewire_data >> 1); // shift bits right, first received = bit0
                    if ((PIN(RESPONSEWIRE_PORT) & _BV(RESPONSEWIRE_PIN)) == 0) // phsyical low == logic 1
                        responsewire_data |= 0b1000000000000000;
                    _delay_us(48); // get to next bit
                }
                // we have some little time here to decode the response and do some action for refueling state of cars
                decode_responsewire();
            }
            TIMSK |= _BV(OCIE2); //enable timer2 interrupt
            // end reading response wire


        }
    } else {
        uint16_t bit = (1<<(transmit_len & 0b01111111));
        uint16_t clock;
        if ((transmit_len & 0b10000000) == 0) clock = 0; else clock = 0xffff;
        if ( ((transmit_buffer ^ clock) & bit) != 0 )
            RAIL_POWER_PORT |= _BV(RAIL_POWER); else
            RAIL_POWER_PORT &= ~_BV(RAIL_POWER);
        if ( (transmit_len & 0b10000000) == 0 ) {
            // block 0
            //if (transmit_len == 0) transmit_len = 0xFF; else transmit_len |= 0b10000000; // set clock
            transmit_len |= 0b10000000; // set clock
        } else {
            // block 1, output the current bit
            transmit_len &= 0b01111111; // reset clock
            //if (transmit_len != 0) transmit_len--; // next bit
            if (transmit_len == 0) transmit_len = 0xFE; else transmit_len--; // next bit
        }
    }
}


ISR ( TIMER0_OVF_vect ) {
    if (mode==0xff) return; // panic mode, drop rest of routine
// TODO: last bit should be set by the sender, not from us!
    TCNT0 = TIMER0_250US;
    if (timer0_delay == 0) {
        RAIL_POWER_PORT &= ~_BV(RAIL_POWER); // pull rails low
        _delay_us(28); // wait some cycles
        if ((PIN(RAIL_DETECT_PORT) & _BV(RAIL_DETECT)) != 0) { // check for logic zero
            if (response == 0) {
                // there is no start bit, so stop the timer and cancel response receiving
                TIMSK &= ~_BV(TOIE0);
            } else {
                // we received a bit (logic low)
                response = response << 1;
                response_len++;
            }
        } else {
            // okay, we have logic high
            response = response << 1;
            response |= 1;
            response_len++;
        }
        if (response_len == 15) { // maximum response length reached
            RAIL_POWER_PORT |= _BV(RAIL_POWER); // restore rails power
            TIMSK &= ~_BV(TOIE0);
        } else {
            _delay_us(20); // wait some cycles
            RAIL_POWER_PORT |= _BV(RAIL_POWER); // restore rails power
        }
    } else timer0_delay--; // 2.3 ms delay not reached yet
}

ISR (INT0_vect) {
    // car0 detector
    uint16_t tmp = 0;
    car0_new = TCNT1; // get current counter
    if (car0_old < car0_new) {
        // calculate difference
        if (car0 == 0) tmp = car0_new-car0_old;
        if ( (tmp > 54) && (tmp < 74) ) car0 = 1;
        if ( (tmp > 118) && (tmp < 138) ) car0 = 2;
        if ( (tmp > 186) && (tmp < 206) ) car0 = 3;
        if ( (tmp > 246) && (tmp < 266) ) car0 = 4;
        if ( (tmp > 310) && (tmp < 330) ) car0 = 5;
        if ( (tmp > 374) && (tmp < 394) ) car0 = 6;
    }
    car0_old = car0_new;
}

ISR (INT1_vect) {
    // car1 detector
    uint16_t tmp = 0;
    car1_new = TCNT1; // get current counter
    if (car1_old < car1_new) {
        // calculate difference
        if (car1 == 0) tmp = car1_new-car1_old;
        if ( (tmp > 54) && (tmp < 74) ) car1 = 1;
        if ( (tmp > 118) && (tmp < 138) ) car1 = 2;
        if ( (tmp > 186) && (tmp < 206) ) car1 = 3;
        if ( (tmp > 246) && (tmp < 266) ) car1 = 4;
        if ( (tmp > 310) && (tmp < 330) ) car1 = 5;
        if ( (tmp > 374) && (tmp < 394) ) car1 = 6;
    }
    car1_old = car1_new;
}


ISR (INT2_vect) {
    // Lap counter Interrupt
    // do not know if this ever occurs ?! this is normally an output pin to trigger the counter start
}
