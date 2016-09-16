OpenSlotDirector
================

Fork of OpenSlot.de with the main goal to adapt the firmware to use the blackbox for an analog Carrera track.
The code is hosted at https://github.com/S-I-M-O-N/openslot.de
The original OpenSlot.de code and hardware mod can be found at http://www.openslot.de

In contrast to the original hardware mod the foto transistor has to be installed in the slot itself.
With a IR led on the opposite side. When a slot car passes this position it will interupt the light beam and trigger the counter.
```
   _______    ________
         |   |
   ===O ---------> O=====
   LED   |___|  foto transistor
          slot
```
I used a LED and foto transistor I had from another project:
LED: 			SFH487-2
Foto transistor:	SFH309FA

The foto transistor is connected to the blackbox in the original openslot.de project, but I found that it triggered more reliable after I removed C37, C39, R47 and R49.

The LED needs to be connected to 5V with a resistor in series to limit the current.

TODO: Check value used and connection of transistor.


To connect the display the MODUL port can be used as it contains the I2C bus lines. See [Slotbaer's D133 project](http://www.slotbaer.de/index.php/slotbaer-projekte-digital/28-pd132-d124/29-d133) for the pin out. A 5V pin is missing though, you can use pin 3 for that (same position as on PC-UNIT port) as it is unused. Just use a wire to connect the pin for connector pin 3 to 5V. If you want you can do that for both MODUL ports giving you full flexibility were to attach the display.

I used a [I2C LCD2004](http://www.sunfounder.com/wiki/index.php?title=I2C_LCD2004) display from sunfounder. 	

