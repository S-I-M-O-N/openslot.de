PRG=main

MCU=atmega16
#FUSES=-U lfuse:w:0x3f:m -U hfuse:w:0xd8:m
FUSES=-U lfuse:w:0x2f:m -U hfuse:w:0xc8:m
BOOTFUSES=-U lfuse:w:0xff:m -U hfuse:w:0xce:m

#F_CPU=4185600
#F_CPU = 14318000

F_CPU = 8000000

BAUD=57600
ISP_BAUD=115200
ifdef USB
PORT=/dev/ttyUSB$(USB)
else
PORT=/dev/ttyS0
endif

PROGRAMMER=-P $(PORT) -c stk500v2
ifdef IF
 ifdef USB
 PROGRAMMER=-P $(PORT) -c $(IF)
 else
 PROGRAMMER=-c $(IF)
 endif
endif

#SRC = main.c seriald.c driver/ADC.c driver/clock.c driver/timer.c
#SRC = main.c driver/rs232.c driver/manchester.c
SRC = main.c driver/rs232.c driver/adc.c
SRC += lowlevel.c

###################################################################
# You possibly do not need to change settings below this marker
###################################################################

# Binaries to be used
# You may add the path to them if they are not in the PATH variable.
CC      = avr-gcc
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
AVRDUDE = avrdude
PERL    = perl

# Optional library search path
LIBS =

# Compiler options for all c source files
CFLAGS += -g -Wall -mmcu=$(MCU) -DBAUD=$(BAUD) -DF_CPU=$(F_CPU)UL -std=gnu99 
CFLAGS += -funsigned-char
CFLAGS += -funsigned-bitfields
CFLAGS += -fpack-struct
CFLAGS += -fshort-enums
CFLAGS += -Wstrict-prototypes
CFLAGS += -Wundef
#CFLAGS += -save-temps

# optimize for size
CFLAGS += -Os
# dont optimize
#CFLAGS += -O0

# Linker options
LDFLAGS = -Wl,-Map,$(PRG).map

# Enable floating-point support in printf
#LDFLAGS += -Wl,-u,vfprintf -lprintf_flt -lm

###################################################################
# TARGET DEFINITIONS:


all: code

code: $(PRG).hex

$(PRG).elf: $(SRC:.c=.o)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

%.lst: %.elf
	$(OBJDUMP) -h -S $< > $@

%.hex: %.elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@
	rm $(PRG).elf
	rm $(PRG).map

program: code
	$(AVRDUDE) $(PROGRAMMER) -b $(ISP_BAUD) -i 1 -p $(MCU) -V -U flash:w:$(PRG).hex:i

backup:
	$(AVRDUDE) $(PROGRAMMER) -b $(ISP_BAUD) -i 1 -p $(MCU) -V -U flash:r:$(PRG)_backup.hex:i

restore:
	$(AVRDUDE) $(PROGRAMMER) -b $(ISP_BAUD) -i 1 -p $(MCU) -V -U flash:w:$(PRG).hex:i

fuse:
	$(AVRDUDE) $(PROGRAMMER) -p $(MCU) -V $(FUSES)

clean:
	rm -rf *.o *.elf *.elf.src *.s *.i
	rm -rf driver/*.o

upgrade: code
	$(RESETCOMMAND)
	./bootloader -d $(NETDEV) -b $(UPGRADE_BAUD) -p $(PRG).hex

bootloader: bootload.hex
	$(AVRDUDE) -p $(MCU) -c stk500 -V -U flash:w:bootload.hex:i

bootfuses:
	$(AVRDUDE) -p $(MCU) -c stk500 $(BOOTFUSES)
