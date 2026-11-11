/* avr-gcc 16.1 / 16.2.1 / 17 (master 2026-11-11): ICE in simplify_gen_subreg_concatn
 * (lower-subreg.cc), pass subreg3, on a paradoxical subreg of a decomposed HImode pseudo.
 *   avr-gcc -mmcu=atxmega256a3u -Os -S ice-lower-subreg.c
 * Reduced from src/api.c (set_delay_pair). Fixed by
 * patches/0001-lower-subreg-paradoxical-subreg-of-concatn.patch. */
typedef unsigned char uint8_t;
typedef unsigned int uint16_t;
typedef long unsigned int uint32_t;
static inline uint8_t bas_data_byte(const uint8_t *p) { return
                                                              (__extension__({ uint16_t __addr16 = (uint16_t) ((uint16_t)(
                                                              p
                                                              )); uint8_t __result; __asm __volatile__ ("lpm %0,%a1 ; [[len=nl]]" : "=r" (__result) : "z" (__addr16)); __result; }))
                                                                              ; }
extern uint8_t dsp_core, dsp_reg;
void dsp_set_delay(uint32_t value);
static const uint8_t data_flow_pair_delay_dsp[9]
__attribute__((__progmem__))
                                           = {
 0x00, 0x03, 0x03, 0x03, 0x03, 0x02, 0x02, 0x02, 0x02,
};
static const uint8_t data_flow_pair_delay_reg[9]
__attribute__((__progmem__))
                                           = {
 0x00, 0xD1, 0xD1, 0xD0, 0xD0, 0xD1, 0xD1, 0xD0, 0xD0,
};
extern uint8_t storage_non_biquadvalues[678 + 1];
extern uint8_t storage_flags, storage_flags_check;
uint8_t api_store = 1;
static uint16_t sw_get(uint16_t base, uint8_t i)
{
 uint16_t a = (uint16_t)(base + (i - 1) * 2);
 return storage_non_biquadvalues[a] | (uint16_t)storage_non_biquadvalues[a + 1] << 8;
}
static void sw_set(uint16_t base, uint8_t i, uint16_t v)
{
 uint16_t a = (uint16_t)(base + (i - 1) * 2);
 storage_non_biquadvalues[a] = v & 0xFF;
 storage_non_biquadvalues[a + 1] = v >> 8;
}
static void set_delay_pair(uint16_t base, uint8_t ch, uint16_t v, const uint8_t *tab_dsp, const uint8_t *tab_reg)
{
 uint16_t w1, w2;
 if (ch % 2 == 1) {
  w1 = sw_get(base, (uint8_t)(ch + 1));
  w2 = v;
 } else {
  w1 = v;
  w2 = sw_get(base, (uint8_t)(ch - 1));
 }
 dsp_core = bas_data_byte(&(tab_dsp)[(uint16_t)(ch)]);
 dsp_reg = bas_data_byte(&(tab_reg)[(uint16_t)(ch)]);
 dsp_set_delay((uint32_t)w1 | (uint32_t)w2 << 16);
}
void api_set_delay(uint8_t ch, uint16_t v)
{
 set_delay_pair(484, ch, v, data_flow_pair_delay_dsp, data_flow_pair_delay_reg);
 if (api_store == 1) {
  sw_set(484, ch, v);
  storage_flags++;
 }
}
uint16_t api_get_delay(uint8_t ch) { return sw_get(484, ch); }
