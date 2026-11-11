#ifndef UART_H_
#define UART_H_
void uart_init(void);
int uart_getc(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_flush(void);
#endif
