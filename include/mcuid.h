#ifndef MCUID_H_
#define MCUID_H_
/* 16 chars as init_mcuid (libs/misc/mcuid): LOTNUM0..5 raw, hex(WAFNUM), hex(COORDX0..COORDY1) */
extern char mcuid_string[17];
void mcuid_init(void);
#endif
