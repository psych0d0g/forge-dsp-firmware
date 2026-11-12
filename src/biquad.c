/*
 * Port of the biquad math of libs/dsp/tas3108/functions.bas. Formulas, constants and the
 * order of operations are kept as in BASCOM (same double arithmetic).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include <math.h>
#include "bas_data.h"
#include "biquad.h"
#include "flavour.h"
#include "flavour_data.h"

double dsp_bq_cache_values[7];
uint8_t dsp_bq_cache_type;

#define V(i) dsp_bq_cache_values[i]
#define F    BAS_DSP_BQ_CACHE_VALUE_F
#define Q    BAS_DSP_BQ_CACHE_VALUE_Q
#define GAIN BAS_DSP_BQ_CACHE_VALUE_GAIN
#define BW   BAS_DSP_BQ_CACHE_VALUE_B
#define A0   BAS_DSP_BQ_CACHE_A0
#define A1   BAS_DSP_BQ_CACHE_A1
#define A2   BAS_DSP_BQ_CACHE_A2
#define B0   BAS_DSP_BQ_CACHE_B0
#define B1   BAS_DSP_BQ_CACHE_B1
#define B2   BAS_DSP_BQ_CACHE_B2

static const double dsp_math_double_pi = 6.283185307179586476925286766559;
static double dsp_omega_calc, dsp_alpha_calc, dsp_power_calc, dsp_omega_cos, dsp_q;

static void omega_calc(void) { dsp_omega_calc = dsp_math_double_pi * dsp_omega_calc; dsp_omega_calc = dsp_omega_calc / BAS_FLOW_SAMPLE_RATE; }
static void alpha_calc(void) { double d = sin(dsp_omega_calc); dsp_alpha_calc = 2.0 * dsp_q; dsp_alpha_calc = d / dsp_alpha_calc; }

/* dsp_bq_power: 10^(gain/40) from the table data_power_gain in 0.1 dB steps (0..12 dB).
 * PORT (#51): a gain between two table steps (finer loudness ramps) is computed directly;
 * whole 0.1 dB steps still come from the table, bit identical to the original. */
static void power(void)
{
	double g = dsp_power_calc;
	dsp_power_calc = dsp_power_calc * 10;
	int32_t l = (int32_t)dsp_power_calc;  /* double -> long */
	double frac = dsp_power_calc - (double)l;
	if ((frac > 0.001 || frac < -0.001) && l >= -120 && l <= 120) {
		dsp_power_calc = pow(10.0, g / 40.0);
		return;
	}
	uint8_t neg = 0;
	if (l < 0) {
		neg = 1;
		l = -l;
	}
	uint8_t count = (uint8_t)l;
	if (count > 120)
		count = 0;
	dsp_power_calc = BAS_LOOKUP_D(data_power_gain, count);  /* restore; read count+1 times */
	if (neg)
		dsp_power_calc = 1.0 / dsp_power_calc;
}

static void high_pass_butter_1(void)
{
	dsp_omega_calc = V(F); omega_calc();
	dsp_q = 1.0; alpha_calc();
	double d = 1.0;
	V(A0) = d + dsp_alpha_calc; V(A1) = d - dsp_alpha_calc; V(A2) = 0.0;
	V(B0) = 1.0; V(B1) = -1.0; V(B2) = 0.0;
}

static void high(void)
{
	double d;
	dsp_omega_calc = V(F); omega_calc(); alpha_calc();
	dsp_omega_cos = cos(dsp_omega_calc);
	d = 1.0 + dsp_omega_cos; d = d / 2.0; V(B0) = d;
	d = 1.0 + dsp_omega_cos; d = -d; V(B1) = d;
	V(B2) = V(B0);
	d = 1.0 + dsp_alpha_calc; V(A0) = d;
	d = -2.0 * dsp_omega_cos; d = -d; V(A1) = d;
	d = 1.0 - dsp_alpha_calc; d = -d; V(A2) = d;
}

static void low_pass_butter_1(void)
{
	dsp_omega_calc = V(F); omega_calc();
	dsp_q = 1.0; alpha_calc();
	double d = 1.0;
	V(A0) = d + dsp_alpha_calc; V(A1) = d - dsp_alpha_calc; V(A2) = 0.0;
	V(B0) = d * dsp_alpha_calc; V(B1) = d * dsp_alpha_calc; V(B2) = 0.0;
}

static void low(void)
{
	double d;
	dsp_omega_calc = V(F); omega_calc(); alpha_calc();
	dsp_omega_cos = cos(dsp_omega_calc);
	d = 1.0 - dsp_omega_cos; d = d / 2.0; V(B0) = d;
	d = 1.0 - dsp_omega_cos; V(B1) = d;
	V(B2) = V(B0);
	d = 1.0 + dsp_alpha_calc; V(A0) = d;
	d = -2.0 * dsp_omega_cos; d = -d; V(A1) = d;
	d = 1.0 - dsp_alpha_calc; d = -d; V(A2) = d;
}

/* shelves: bd[1..4] are buffer_doubles(1..4) of BASCOM */
static void shelf_common(double bd[5], double *bso)
{
	dsp_omega_calc = V(F); omega_calc();
	dsp_q = V(Q);
	dsp_power_calc = V(GAIN); power();
	dsp_omega_cos = cos(dsp_omega_calc);
	bd[1] = dsp_power_calc + 1.0;
	bd[2] = dsp_power_calc - 1.0;
	bd[3] = bd[1] * dsp_omega_cos;
	bd[4] = bd[2] * dsp_omega_cos;
	*bso = sqrt(dsp_power_calc);
	*bso = *bso / dsp_q;
	double d = sin(dsp_omega_calc);
	*bso = *bso * d;
}

static void high_shelf(void)
{
	double bd[5], bso, d;
	shelf_common(bd, &bso);
	d = bd[1] + bd[4]; d = d + bso; d = dsp_power_calc * d; V(B0) = d;
	d = bd[2] + bd[3]; d = dsp_power_calc * d; d = -2.0 * d; V(B1) = d;
	d = bd[1] + bd[4]; d = d - bso; d = dsp_power_calc * d; V(B2) = d;
	d = bd[1] - bd[4]; d = d + bso; V(A0) = d;
	d = bd[2] - bd[3]; d = -2.0 * d; V(A1) = d;
	d = bd[1] - bd[4]; d = d - bso; d = -d; V(A2) = d;
}

static void low_shelf(void)
{
	double bd[5], bso, d;
	shelf_common(bd, &bso);
	d = bd[1] - bd[4]; d = d + bso; d = dsp_power_calc * d; V(B0) = d;
	d = bd[2] - bd[3]; d = dsp_power_calc * d; d = 2.0 * d; V(B1) = d;
	d = bd[1] - bd[4]; d = d - bso; d = dsp_power_calc * d; V(B2) = d;
	d = bd[1] + bd[4]; d = d + bso; V(A0) = d;
	d = bd[2] + bd[3]; d = 2.0 * d; V(A1) = d;
	d = bd[1] + bd[4]; d = d - bso; d = -d; V(A2) = d;
}

/* peaking EQ, shared by eq_q (q from the cache) and eq_bandwidth (q = f / b) */
static void eq(void)
{
	double d;
	alpha_calc();
	dsp_omega_cos = cos(dsp_omega_calc);
	dsp_power_calc = V(GAIN); power();
	d = dsp_alpha_calc / dsp_power_calc; d = 1.0 + d; V(A0) = d;
	d = -2.0 * dsp_omega_cos; d = -d; V(A1) = d;
	d = dsp_alpha_calc / dsp_power_calc; d = 1.0 - d; d = -d; V(A2) = d;
	d = dsp_alpha_calc * dsp_power_calc; d = 1.0 + d; V(B0) = d;
	d = -2.0 * dsp_omega_cos; V(B1) = d;
	d = dsp_alpha_calc * dsp_power_calc; d = 1.0 - d; V(B2) = d;
}

static void eq_q(void)
{
	dsp_omega_calc = V(F); omega_calc();
	dsp_q = V(Q);
	eq();
}

static void eq_bandwidth(void)
{
	dsp_omega_calc = V(F); omega_calc();
	dsp_q = V(F); dsp_q = dsp_q / V(BW);
	eq();
}

/* type 17 BandPass (planned in the original, "? 17 : BandPass" in commands.txt, issue #20):
 * the band-pass of the GUI preview (CalculatorBandPass, constant skirt gain, peak gain = Q):
 * b0 = sin(w0)/2, b1 = 0, b2 = -sin(w0)/2, a0 = 1 + alpha, a1 = -2 cos(w0), a2 = 1 - alpha;
 * a1/a2 stored negated as for the other types. Gain is not used. */
static void band_pass(void)
{
	double d;
	dsp_omega_calc = V(F); omega_calc();
	dsp_q = V(Q);
	alpha_calc();
	dsp_omega_cos = cos(dsp_omega_calc);
	d = sin(dsp_omega_calc); d = d / 2.0; V(B0) = d;
	V(B1) = 0.0;
	V(B2) = -V(B0);
	d = 1.0 + dsp_alpha_calc; V(A0) = d;
	d = 2.0 * dsp_omega_cos; V(A1) = d;
	d = 1.0 - dsp_alpha_calc; d = -d; V(A2) = d;
}

static void notch(void)
{
	double d;
	dsp_omega_calc = V(F); omega_calc();
	dsp_q = V(F); dsp_q = dsp_q / V(BW);
	alpha_calc();
	dsp_omega_cos = cos(dsp_omega_calc);
	d = 1.0 + dsp_alpha_calc; V(A0) = d;
	d = 2.0 * dsp_omega_cos; V(A1) = d;
	d = 1.0 - dsp_alpha_calc; d = -d; V(A2) = d;
	V(B0) = 1.0;
	d = -2.0 * dsp_omega_cos; V(B1) = d;
	V(B2) = 1.0;
}

static void bypass(void)
{
	V(A0) = 1.0; V(A1) = 0.0; V(A2) = 0.0; V(B0) = 1.0; V(B1) = 0.0; V(B2) = 0.0;
}

static void riaa(void)
{
	V(A0) = 1.0; V(A1) = 1.866634; V(A2) = -0.8670602;
	V(B0) = 0.1170960; V(B1) = -0.0999452; V(B2) = -0.0129345;
}

static void phaseshift(void)
{
	double d;
	dsp_omega_calc = V(F); omega_calc();
	dsp_q = V(Q); alpha_calc();
	dsp_omega_cos = cos(dsp_omega_calc);
	d = 1.0 + dsp_alpha_calc; V(A0) = d; V(B2) = d;
	d = 2.0 * dsp_omega_cos; V(A1) = d;
	d = -d; V(B1) = d;
	d = 1.0 - dsp_alpha_calc; V(B0) = d;
	d = -d; V(A2) = d;
}

/* PORT extension (#48): first order all pass H = (c + z^-1) / (1 + c z^-1),
 * c = (tan(w0/2) - 1) / (tan(w0/2) + 1) (bilinear with prewarp: -90 deg exactly at f).
 * a1 = c is stored negated as for the other types. k = tan(w0/2). */
static void allpass1_k(double k)
{
	double c = (k - 1.0) / (k + 1.0);
	V(A0) = 1.0; V(A1) = -c; V(A2) = 0.0;
	V(B0) = c; V(B1) = 1.0; V(B2) = 0.0;
}

static void allpass_1(void)
{
	allpass1_k(tan(dsp_math_double_pi / 2.0 * V(F) / BAS_FLOW_SAMPLE_RATE));
}

/* PORT extension (#48): all pass with a wanted phase lag of b degrees at f. The RBJ all pass
 * is the bilinear transform of the analog one with prewarp at f0, so its phase at f is the
 * analog phase at x = tan(pi f/fs) / tan(pi f0/fs):
 *   1st order: phi = -2 atan(x)                          -> x = tan(phi/2)
 *   2nd order: phi = -2 atan2(x/Q, 1 - x^2), theta = phi/2: sin(theta) x^2 + cos(theta)/Q x
 *              - sin(theta) = 0, positive root.
 * Below 180 deg first order (q unused), from 180 deg on second order with q. */
static void phase_angle(void)
{
	double deg = V(BW), q = V(Q);
	double kf = tan(dsp_math_double_pi / 2.0 * V(F) / BAS_FLOW_SAMPLE_RATE);
	if (!(deg >= 1.0 && deg < 360.0) || !(kf > 0.0)) {
		bypass();
		return;
	}
	double th = deg / 2.0 * (dsp_math_double_pi / 360.0);
	if (deg < 180.0) {
		allpass1_k(kf / tan(th));
		return;
	}
	if (!(q > 0.0))
		q = 0.707;
	double s = sin(th), c = cos(th);
	double x = (-c / q + sqrt(c * c / (q * q) + 4.0 * s * s)) / (2.0 * s);
	double w0 = 2.0 * atan(kf / x);
	double alpha = sin(w0) / (2.0 * q), cw = cos(w0);
	V(A0) = 1.0 + alpha; V(B2) = 1.0 + alpha;
	V(A1) = 2.0 * cw; V(B1) = -2.0 * cw;
	V(B0) = 1.0 - alpha; V(A2) = -(1.0 - alpha);
}

void dsp_bq_calc_type(uint8_t type)
{
	switch (type) {
	case BAS_DSP_BQTYPE_EQ_Q_FACTOR:          eq_q(); break;
	case BAS_DSP_BQTYPE_HIGH_PASS_BUTTER_1:   high_pass_butter_1(); break;
	case BAS_DSP_BQTYPE_HIGH_PASS_BUTTER_2:   dsp_q = BAS_DSP_BQ_Q_BUTTERWORTH; high(); break;
	case BAS_DSP_BQTYPE_HIGH_PASS_BESSEL:     dsp_q = BAS_DSP_BQ_Q_BESSEL; high(); break;
	case BAS_DSP_BQTYPE_HIGH_PASS_LINKWITZ:   dsp_q = BAS_DSP_BQ_Q_LINKWITZ; high(); break;
	case BAS_DSP_BQTYPE_HIGH_PASS_VARIABLE_Q: dsp_q = V(Q); high(); break;
	case BAS_DSP_BQTYPE_HIGH_PASS_CHEBYCHEV:  dsp_q = BAS_DSP_BQ_Q_CHEBYCHEV; high(); break;
	case BAS_DSP_BQTYPE_LOW_PASS_BUTTER_1:    low_pass_butter_1(); break;
	case BAS_DSP_BQTYPE_LOW_PASS_BUTTER_2:    dsp_q = BAS_DSP_BQ_Q_BUTTERWORTH; low(); break;
	case BAS_DSP_BQTYPE_LOW_PASS_BESSEL:      dsp_q = BAS_DSP_BQ_Q_BESSEL; low(); break;
	case BAS_DSP_BQTYPE_LOW_PASS_LINKWITZ:    dsp_q = BAS_DSP_BQ_Q_LINKWITZ; low(); break;
	case BAS_DSP_BQTYPE_LOW_PASS_VARIABLE_Q:  dsp_q = V(Q); low(); break;
	case BAS_DSP_BQTYPE_LOW_PASS_CHEBYCHEV:   dsp_q = BAS_DSP_BQ_Q_CHEBYCHEV; low(); break;
	case BAS_DSP_BQTYPE_BASS_SHELF:           low_shelf(); break;
	case BAS_DSP_BQTYPE_TREBLE_SHELF:         high_shelf(); break;
	case BAS_DSP_BQTYPE_NOTCH:                notch(); break;
	case BAS_DSP_BQTYPE_BAND_PASS:            band_pass(); break;  /* PORT: not in the original */
	case BAS_DSP_BQTYPE_EQ_BANDWIDTH:         eq_bandwidth(); break;
	case BAS_DSP_BQTYPE_RIAA:                 riaa(); break;
	case BAS_DSP_BQTYPE_PHASESHIFT:           phaseshift(); break;
	case DSP_BQTYPE_ALLPASS_1:                allpass_1(); break;    /* PORT (#48) */
	case DSP_BQTYPE_PHASE_ANGLE:              phase_angle(); break;  /* PORT (#48) */
	default:                                  bypass();
	}
}

void dsp_bq_calcto5coeff(void)
{
	V(A1) = V(A1) / V(A0);
	V(A2) = V(A2) / V(A0);
	V(B0) = V(B0) / V(A0);
	V(B1) = V(B1) / V(A0);
	V(B2) = V(B2) / V(A0);
	V(A0) = V(A0) / V(A0);
}

/* dsp_math_to523: value * 2^23 -> long, + 2^27, byte 4 xor 0x08 = 28-bit two's complement.
 * PORT: double -> long truncates here (C cast); whether BASCOM rounds is to be checked
 * against biquad...raw of a device (+-1 LSB). */
uint32_t dsp_math_to523(double v)
{
	double d = v * 8388608.0;
	int32_t value223 = (int32_t)d;
	uint32_t value227 = (uint32_t)value223 + 134217728UL;
	return value227 ^ 0x08000000UL;
}

void dsp_bq_convert2raw(uint32_t raw[6])
{
	for (uint8_t nr = 1; nr <= 5; nr++)
		raw[nr] = dsp_math_to523(V(nr));
}

void dsp_bq_default(uint32_t raw[6])
{
	for (uint8_t i = 1; i <= 4; i++)
		raw[i] = 0;
	raw[5] = 0x00800000UL;
}
