/*
 * PORT extension (#47): dynamic biquad pools, generator of the DSP program.
 *
 * The tables come from tools/dsp_pool.py (src/flavours/<f>/dsp_opt.h, dsp_N_pool); the
 * generator here does exactly what Program.generate() there does, tests/test_cmd.py compares
 * the EEPROM images of both. A channel owns consecutive slots of its core's pool; a slot keeps its
 * coefficient address, state words and I2C subaddress, so data_flow_dsp_bq_core/reg and the
 * storage of a virtual biquad number stay valid. Only the program and the channel offsets and
 * limits change.
 * Lengths: MCU EEPROM POOL_EE_MAGIC, then one byte per pool channel; anything that does not fit
 * the pool (blank storage, another flavour) means the default lengths of the original.
 * License: MIT
 */
#include "flavour.h"
#include "bas_data.h"
#include "eram.h"
#include "flavour_data.h"
#include "dsp_pool.h"
#include "store.h"

#ifndef FORGE_DSP_OPT
#define FORGE_DSP_OPT 0
#endif
#if FORGE_DSP_OPT && __has_include("dsp_opt.h")
#include "dsp_opt.h"
#endif

#define POOL_EE_MAGIC_ADDR 0x0F20
#define POOL_EE_LEN_ADDR   0x0F21  /* DSP_POOL_CHANNELS bytes */
#define POOL_EE_MAGIC      0x47

#ifdef DSP_POOL
/* the stored values of the spare biquads must fit the preset areas of the original */
_Static_assert((BAS_FLOW_MAX_DSP_BIQUADS + DSP_POOL_EXTRA + 1) * BAS_STORAGE_BQTV_VALUES_FRAMESIZE <= BAS_LIMIT_STORE_ADDR_BQTV,
               "bqtv area too small for the spare biquads");
/* 393: data_flow_bq_storage_offset of core 1 in all pool flavours */
_Static_assert((BAS_FLOW_MAX_DSP_BIQUADS + DSP_POOL_EXTRA + 1 - 393) * BAS_DSP_REG_SIZE_BIQUAD_CHKSUM <= BAS_LIMIT_STORE_ADDR_BQ,
               "biquad area of core 1 too small for the spare biquads");
#endif

/* table layout (tools/dsp_pool.py, Program.c_table) */
enum { H_PROG_OFF, H_PROG_LEN, H_PIECES, H_CHAINS, H_SEGS, H_SLOTS, H_PATCHES, H_CHANNELS,
       O_PIECES, O_CHAINS, O_SEGS, O_SLOTS, O_PATCHES, O_CHANNELS, O_TPL };
enum { T_H0, T_H1, T_M, T_P1, T_P2, T_P3, T_T0, T_T1, T_T2 };

static inline uint16_t tw(const uint8_t *t, uint16_t a) { return bas_data_word(t + a); }
static inline uint8_t tb(const uint8_t *t, uint16_t a) { return bas_data_byte(t + a); }
static inline uint16_t hdr(const uint8_t *t, uint8_t i) { return tw(t, (uint16_t)(2 * i)); }

#ifdef DSP_POOL
static uint8_t pool_len[DSP_POOL_CHANNELS];
static uint8_t pool_loaded;

static const uint8_t *core_table(uint8_t core) { return core == 1 ? dsp_1_pool : dsp_2_pool; }

uint8_t dsp_pool_cores(void) { return DSP_POOL_CORES; }
uint8_t dsp_pool_channels(uint8_t core, uint8_t *first)
{
	if (core < 1 || core > DSP_POOL_CORES)
		return 0;
	*first = dsp_pool_core_first[core - 1];
	return (uint8_t)(dsp_pool_core_first[core] - *first);
}
static uint8_t core_of(uint8_t q)
{
	uint8_t k = 1;
	while (q >= dsp_pool_core_first[k])
		k++;
	return k;
}
uint8_t dsp_pool_channel(uint8_t q) { return q < DSP_POOL_CHANNELS ? dsp_pool_chan[q] : 0; }
static uint16_t chan_rec(uint8_t q)  /* table offset of the channel record of pool index q */
{
	uint8_t k = core_of(q);
	const uint8_t *t = core_table(k);
	return (uint16_t)(hdr(t, O_CHANNELS) + 3 * (q - dsp_pool_core_first[k - 1]));
}
uint8_t dsp_pool_min(uint8_t q) { return tb(core_table(core_of(q)), (uint16_t)(chan_rec(q) + 2)); }
static uint8_t pool_default(uint8_t q) { return tb(core_table(core_of(q)), (uint16_t)(chan_rec(q) + 1)); }
uint16_t dsp_pool_size(uint8_t core) { return (core >= 1 && core <= DSP_POOL_CORES) ? hdr(core_table(core), H_SLOTS) : 0; }

uint8_t dsp_pool_check(const uint8_t *len)
{
	for (uint8_t k = 1; k <= DSP_POOL_CORES; k++) {
		uint16_t sum = 0;
		for (uint8_t q = dsp_pool_core_first[k - 1]; q < dsp_pool_core_first[k]; q++) {
			if (len[q] < dsp_pool_min(q))
				return 1;
			sum = (uint16_t)(sum + len[q]);
		}
		if (sum > dsp_pool_size(k))  /* (#47 step 7) the spare biquads may stay unused */
			return 1;
	}
	return 0;
}

void dsp_pool_load(void)
{
	uint8_t len[DSP_POOL_CHANNELS];
	for (uint8_t q = 0; q < DSP_POOL_CHANNELS; q++)
		len[q] = eram_rb((uint16_t)(POOL_EE_LEN_ADDR + q));
	if (eram_rb(POOL_EE_MAGIC_ADDR) != POOL_EE_MAGIC || dsp_pool_check(len))
		for (uint8_t q = 0; q < DSP_POOL_CHANNELS; q++)
			len[q] = pool_default(q);
	for (uint8_t q = 0; q < DSP_POOL_CHANNELS; q++)
		pool_len[q] = len[q];
	pool_loaded = 1;
}

void dsp_pool_store(const uint8_t *len)
{
	for (uint8_t q = 0; q < DSP_POOL_CHANNELS; q++)
		eram_wb((uint16_t)(POOL_EE_LEN_ADDR + q), len[q]);
	eram_wb(POOL_EE_MAGIC_ADDR, POOL_EE_MAGIC);
	dsp_pool_load();
}

uint8_t dsp_pool_len(uint8_t q)
{
	if (!pool_loaded)
		dsp_pool_load();
	return q < DSP_POOL_CHANNELS ? pool_len[q] : 0;
}

static int8_t pool_index(uint8_t ch)
{
	for (uint8_t q = 0; q < DSP_POOL_CHANNELS; q++)
		if (dsp_pool_chan[q] == ch)
			return (int8_t)q;
	return -1;
}

uint8_t flow_bq_limit(uint8_t ch)
{
	int8_t q = pool_index(ch);
	return q < 0 ? BAS_LOOKUP_B(data_flow_ch_bq_limits, ch) : dsp_pool_len((uint8_t)q);
}

uint16_t flow_bq_offset(uint8_t ch)
{
	int8_t q = pool_index(ch);
	if (q < 0)
		return BAS_LOOKUP_W(data_flow_ch_bq_offsets, ch);
	uint8_t k = core_of((uint8_t)q);
	uint8_t q0 = dsp_pool_core_first[k - 1];
	uint16_t o = BAS_LOOKUP_W(data_flow_ch_bq_offsets, dsp_pool_chan[q0]);  /* start of the pool */
	for (uint8_t i = q0; i < (uint8_t)q; i++)
		o = (uint16_t)(o + dsp_pool_len(i));
	return o;
}

/* virtual biquad numbers: the original 1 … BAS_FLOW_MAX_DSP_BIQUADS, then the spares of core 1 */
uint16_t flow_bq_max(void) { return (uint16_t)(BAS_FLOW_MAX_DSP_BIQUADS + DSP_POOL_EXTRA); }
uint8_t flow_bq_core(uint16_t v)
{
	return v <= BAS_FLOW_MAX_DSP_BIQUADS ? BAS_LOOKUP_B(data_flow_dsp_bq_core, v) : 1;
}
uint8_t flow_bq_reg(uint16_t v)
{
	if (v <= BAS_FLOW_MAX_DSP_BIQUADS)
		return BAS_LOOKUP_B(data_flow_dsp_bq_reg, v);
	v = (uint16_t)(v - BAS_FLOW_MAX_DSP_BIQUADS - 1);
	return v < DSP_POOL_EXTRA ? dsp_pool_extra_reg[v] : 0;
}
uint8_t flow_bq_used(uint16_t v)
{
	for (uint8_t q = 0; q < DSP_POOL_CHANNELS; q++) {
		uint16_t o = flow_bq_offset(dsp_pool_chan[q]);
		if (v > o && v <= o + dsp_pool_len(q))
			return 1;
	}
	return 0;
}

/* New lengths for the channels of one core: the stored biquads move with their channel (band b
 * stays band b), a channel keeps min(old, new) bands, new bands start as defaults; then the lengths
 * are stored. The DSP program follows at the next start (dsp_eeprom_init).
 * The move is an increasing map of virtual numbers: the ones that move down are copied in
 * ascending order, the ones that move up in descending order, so no source is overwritten
 * before it is copied. -> 0 ok, 1 lengths do not fit the pool */
uint8_t dsp_pool_set(uint8_t core, const uint8_t *len)
{
	uint8_t first, n = dsp_pool_channels(core, &first);
	uint8_t all[DSP_POOL_CHANNELS];
	if (n == 0 || n > 8)
		return 1;
	for (uint8_t q = 0; q < DSP_POOL_CHANNELS; q++)
		all[q] = dsp_pool_len(q);
	for (uint8_t i = 0; i < n; i++)
		all[first + i] = len[i];
	if (dsp_pool_check(all))
		return 1;
	uint16_t o_old[8], o_new[8];
	uint8_t keep[8];
	uint16_t a = BAS_LOOKUP_W(data_flow_ch_bq_offsets, dsp_pool_chan[first]), b = a;
	for (uint8_t i = 0; i < n; i++) {
		uint8_t lo = dsp_pool_len((uint8_t)(first + i));
		o_old[i] = a;
		o_new[i] = b;
		keep[i] = lo < len[i] ? lo : len[i];
		a = (uint16_t)(a + lo);
		b = (uint16_t)(b + len[i]);
	}
	for (uint8_t i = 0; i < n; i++)
		for (uint8_t j = 1; j <= keep[i]; j++)
			if (o_new[i] < o_old[i])
				store_bq_move((uint16_t)(o_old[i] + j), (uint16_t)(o_new[i] + j));
	for (int8_t i = (int8_t)(n - 1); i >= 0; i--)
		for (uint8_t j = keep[i]; j >= 1; j--)
			if (o_new[i] > o_old[i])
				store_bq_move((uint16_t)(o_old[i] + j), (uint16_t)(o_new[i] + j));
	for (uint8_t i = 0; i < n; i++)
		for (uint16_t j = (uint16_t)(keep[i] + 1); j <= len[i]; j++)
			store_bq_clear((uint16_t)(o_new[i] + j));
	dsp_pool_store(all);
	return 0;
}

/* --- generator --- */
static void put_word(struct dsp_img *g, uint64_t v)
{
	for (uint8_t i = 0; i < 7; i++) {
		g->w[i] = (uint8_t)v;
		v >>= 8;
	}
}
static uint64_t tpl(const uint8_t *t, uint8_t n)
{
	uint16_t a = (uint16_t)(hdr(t, O_TPL) + 7 * n);
	uint64_t v = 0;
	for (int8_t i = 6; i >= 0; i--)
		v = v << 8 | tb(t, (uint16_t)(a + i));
	return v;
}
static uint64_t mk(const uint8_t *t, uint8_t n, uint16_t d1, uint16_t d2, uint16_t d3)
{
	return tpl(t, n) | (uint64_t)d1 << 27 | (uint64_t)d2 << 14 | d3;
}
static uint16_t slot(const uint8_t *t, uint16_t s, uint8_t f)  /* f: 0 coef, 1 y1, 2 y2 */
{
	return tw(t, (uint16_t)(hdr(t, O_SLOTS) + 6 * s + 2 * f));
}

/* next program word into g->w; 0 at the end */
static uint8_t gen_word(struct dsp_img *g)
{
	const uint8_t *t = g->t;
	for (;;) {
		if (g->piece >= hdr(t, H_PIECES))
			return 0;
		uint16_t pa = (uint16_t)(hdr(t, O_PIECES) + 4 * g->piece);
		uint16_t a = tw(t, pa), b = tw(t, (uint16_t)(pa + 2));
		if (!(a & 0x8000)) {
			if (g->k >= b) {
				g->piece++;
				g->k = 0;
				continue;
			}
			uint16_t i = (uint16_t)(a + g->k++);
			const uint8_t *src = g->img + hdr(t, H_PROG_OFF) + 12 + 7u * i;
			uint64_t v = 0;
			for (int8_t j = 6; j >= 0; j--)
				v = v << 8 | bas_data_byte(src + j);
			if (g->patch < hdr(t, H_PATCHES)) {
				uint16_t r = (uint16_t)(hdr(t, O_PATCHES) + 4 * g->patch);
				if (tw(t, r) == i) {
					uint16_t d = g->dst[tb(t, (uint16_t)(r + 2))];
					uint8_t m = tb(t, (uint16_t)(r + 3));
					if (m & 1)
						v = (v & ~((uint64_t)0x3FF << 27)) | (uint64_t)d << 27;
					if (m & 2)
						v = (v & ~(uint64_t)0x3FF) | d;
					g->patch++;
				}
			}
			put_word(g, v);
			return 1;
		}
		uint8_t c = (uint8_t)(a >> 8 & 0x7F), sk = (uint8_t)a;
		uint16_t cr = (uint16_t)(hdr(t, O_CHAINS) + 4 * c);
		uint8_t q = tb(t, cr), nseg = tb(t, (uint16_t)(cr + 1));
		uint16_t s0 = tw(t, (uint16_t)(cr + 2));
		if (g->k == 0) {  /* start of a segment: its slots */
			uint16_t first = 0;
			for (uint8_t j = 0; j < sk; j++)
				first = (uint16_t)(first + tb(t, (uint16_t)(hdr(t, O_SEGS) + 10 * (s0 + j))));
			uint8_t len = dsp_pool_len((uint8_t)(g->q0 + q));
			if (len <= first) {  /* segment vanished */
				g->piece++;
				continue;
			}
			uint16_t sr = (uint16_t)(hdr(t, O_SEGS) + 10 * (s0 + sk));
			uint8_t n_orig = tb(t, sr);
			uint16_t rest = (uint16_t)(len - first);
			g->seg_n = (uint8_t)((sk == nseg - 1 || rest <= n_orig) ? rest : n_orig);
			g->eo = tb(t, (uint16_t)(sr + 1));
			g->eb = tw(t, (uint16_t)(sr + 2));
			g->x1 = tw(t, (uint16_t)(sr + 4));
			g->x2 = tw(t, (uint16_t)(sr + 6));
			g->slot0 = (uint16_t)(g->off[q] + first);
			g->src = tw(t, (uint16_t)(sr + 8));
			if (g->src == 0xFFFF)
				g->src = slot(t, (uint16_t)(g->slot0 - 1), 1);
			g->seg_lines = (uint16_t)(5 * g->seg_n + 3);
		}
		uint16_t l = g->k++;
		uint64_t v;
		if (l < 5) {
			uint16_t co = slot(t, g->slot0, 0), y1 = slot(t, g->slot0, 1), y2 = slot(t, g->slot0, 2);
			switch (l) {
			case 0: v = mk(t, T_H0, g->x2, (uint16_t)(co + 2), 0); break;
			case 1: v = mk(t, T_H1, g->x1, (uint16_t)(co + 1), 0); break;
			case 2: v = mk(t, T_M, g->src, co, g->x2); break;
			case 3: v = mk(t, T_M, y1, (uint16_t)(co + 3), g->x1); break;
			default: v = mk(t, T_M, y2, (uint16_t)(co + 4), y2); break;
			}
		} else if (l < 5u * g->seg_n) {
			uint16_t j = (uint16_t)((l - 5) / 5 + 1), r = (uint16_t)((l - 5) % 5);
			uint16_t s = (uint16_t)(g->slot0 + j);
			uint16_t co = slot(t, s, 0), y1 = slot(t, s, 1), y2 = slot(t, s, 2);
			uint16_t py1 = slot(t, (uint16_t)(s - 1), 1), py2 = slot(t, (uint16_t)(s - 1), 2);
			switch (r) {
			case 0: v = mk(t, T_P1, 0, (uint16_t)(co + 2), 0); break;
			case 1: v = mk(t, T_P2, 0, co, 0); break;
			case 2: v = mk(t, T_P3, py1, (uint16_t)(co + 1), py1); break;
			case 3: v = mk(t, T_M, y1, (uint16_t)(co + 3), py2); break;
			default: v = mk(t, T_M, y2, (uint16_t)(co + 4), y2); break;
			}
		} else if (l == 5u * g->seg_n) {
			v = tpl(t, T_T0);
		} else if (l == 5u * g->seg_n + 1) {
			v = tpl(t, T_T1);
		} else {
			v = mk(t, T_T2, 0, 0, slot(t, (uint16_t)(g->slot0 + g->seg_n - 1), 1));
		}
		if (l == g->eo)
			v |= g->eb;
		if (g->k >= g->seg_lines) {
			g->piece++;
			g->k = 0;
		}
		put_word(g, v);
		return 1;
	}
}

static void gen_reset(struct dsp_img *g)
{
	const uint8_t *t = g->t;
	g->piece = g->k = g->patch = 0;
	uint16_t o = 0;
	uint8_t nq = (uint8_t)hdr(t, H_CHANNELS);
	for (uint8_t q = 0; q < nq && q < 8; q++) {
		g->off[q] = o;
		o = (uint16_t)(o + dsp_pool_len((uint8_t)(g->q0 + q)));
	}
	for (uint8_t c = 0; c < hdr(t, H_CHAINS) && c < 8; c++) {
		uint8_t q = tb(t, (uint16_t)(hdr(t, O_CHAINS) + 4 * c));
		g->dst[c] = slot(t, (uint16_t)(g->off[q] + dsp_pool_len((uint8_t)(g->q0 + q)) - 1), 1);
	}
}
#else
uint8_t flow_bq_limit(uint8_t ch) { return BAS_LOOKUP_B(data_flow_ch_bq_limits, ch); }
uint16_t flow_bq_max(void) { return BAS_FLOW_MAX_DSP_BIQUADS; }
uint8_t flow_bq_core(uint16_t v) { return BAS_LOOKUP_B(data_flow_dsp_bq_core, v); }
uint8_t flow_bq_reg(uint16_t v) { return BAS_LOOKUP_B(data_flow_dsp_bq_reg, v); }
uint8_t flow_bq_used(uint16_t v) { (void)v; return 1; }
uint16_t flow_bq_offset(uint8_t ch) { return BAS_LOOKUP_W(data_flow_ch_bq_offsets, ch); }
uint8_t dsp_pool_cores(void) { return 0; }
uint8_t dsp_pool_channels(uint8_t core, uint8_t *first) { (void)core; *first = 0; return 0; }
uint8_t dsp_pool_channel(uint8_t q) { (void)q; return 0; }
uint8_t dsp_pool_len(uint8_t q) { (void)q; return 0; }
uint8_t dsp_pool_min(uint8_t q) { (void)q; return 0; }
uint16_t dsp_pool_size(uint8_t core) { (void)core; return 0; }
uint8_t dsp_pool_check(const uint8_t *len) { (void)len; return 1; }
void dsp_pool_store(const uint8_t *len) { (void)len; }
uint8_t dsp_pool_set(uint8_t core, const uint8_t *len) { (void)core; (void)len; return 1; }
void dsp_pool_load(void) {}
#endif

/* level meter (#52): data word of core 2/3 that the program sends to DO1..DO4 (k = 0..3),
 * 0 = unknown (program without the generated tables) */
uint16_t dsp_do_word(uint8_t k)
{
#ifdef DSP_POOL
	if (k < 4)
		return dsp_2_do_word[k];
#endif
	(void)k;
	return 0;
}

/* --- image stream: base bytes before the program block, the new block, base bytes after --- */
void dsp_img_open(struct dsp_img *g, uint8_t core, const uint8_t *code)
{
	g->img = code + 2;
	g->base_size = bas_data_word(code);
	g->size = g->base_size;
	g->pos = 0;
	g->t = 0;
	g->done = 0;
	(void)core;
#ifdef DSP_POOL
	if (core >= 1 && core <= DSP_POOL_CORES) {
		g->t = core_table(core);
		g->q0 = dsp_pool_core_first[core - 1];
		gen_reset(g);
		uint16_t n = 0, sum = 0;
		while (gen_word(g)) {
			n++;
			for (uint8_t i = 0; i < 7; i++)
				sum = (uint16_t)(sum + g->w[i]);
		}
		uint16_t total = (uint16_t)(12 + 7 * n + 7);
		sum = (uint16_t)(sum + 0x1F + 2 + (total >> 8) + (total & 0xFF));   /* header bytes */
		g->nwords = n;
		g->sum = sum;
		g->size = (uint16_t)(g->base_size - hdr(g->t, H_PROG_LEN) + total);
		gen_reset(g);
		g->wi = 7;
	}
#endif
}

uint8_t dsp_img_next(struct dsp_img *g)
{
	uint16_t p = g->pos++;
	if (!g->t)
		return bas_data_byte(g->img + p);
#ifdef DSP_POOL
	uint16_t po = hdr(g->t, H_PROG_OFF);
	uint16_t total = (uint16_t)(12 + 7 * g->nwords + 7);
	if (p < po)
		return bas_data_byte(g->img + p);
	uint16_t r = (uint16_t)(p - po);
	if (r >= total)
		return bas_data_byte(g->img + po + hdr(g->t, H_PROG_LEN) + (r - total));
	if (r < 12) {
		static const uint8_t h[10] = {0x00, 0x1F, 0x02, 0, 0, 0, 0, 0, 0, 0};
		if (r == 0) return (uint8_t)(g->sum >> 8);
		if (r == 1) return (uint8_t)g->sum;
		if (r == 8) return (uint8_t)(total >> 8);
		if (r == 9) return (uint8_t)total;
		return h[r - 2];
	}
	if (r >= total - 7) {
		if (r == total - 2) return (uint8_t)(g->sum >> 8);
		if (r == total - 1) return (uint8_t)g->sum;
		return 0;
	}
	if (g->wi >= 7) {
		gen_word(g);
		g->wi = 0;
	}
	return g->w[g->wi++];
#else
	return 0;
#endif
}
