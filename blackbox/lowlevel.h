#ifndef LOWLEVEL_H
#define LOWLEVEL_H
#include "main.h"

void LED(uint8_t num, uint8_t state);
void LEDS_OFF(void);
void init_hardware(void);
void panic_mode(void);
//uint8_t check_rails_shortcut(void);

#endif

