#ifndef __RS232_H__
#define __RS232_H__

#define RS232_BUFSIZE   10

extern void RS232_init(void);
extern void RS232_putc(char c);
extern void RS232_puts(char* s);
extern void RS232_puts_p(const char* s);


#endif