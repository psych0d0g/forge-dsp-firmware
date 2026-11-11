#ifndef SYS_INIT_H
#define SYS_INIT_H
#include <stdint.h>
/* start commands (libs/sys/init, PORT extension #40: system.startup.N) */
void sys_init_init(void);
void sys_init_mainloop(void);
void sys_init_preset_loaded(void);           /* first preset load done: run the commands */
uint8_t sys_init_get(uint8_t i, char *out);  /* out: ERAM_SYS_INIT_SETS_ERAM_SIZE + 1 bytes */
#endif
