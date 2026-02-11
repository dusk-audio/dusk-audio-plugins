/*
 * WCS Native Engine — C implementation for fast coefficient optimization.
 * Called from Python via ctypes. Mirrors Suede200Reverb.h exactly.
 *
 * Compile: cc -O3 -shared -o wcs_native.so wcs_native.c -lm
 *          (macOS: cc -O3 -shared -o wcs_native.dylib wcs_native.c -lm)
 */

#include <math.h>
#include <string.h>
#include <stdlib.h>

#define MAX_MEMORY (65536 * 4 + 16)  /* Max for ~4x oversampling */
#define NUM_STEPS 128
#define NUM_REGS 8

/* Decoded step fields — flat arrays for cache efficiency */
typedef struct {
    int ofst[NUM_STEPS];
    int cCode[NUM_STEPS];
    int acc0[NUM_STEPS];
    int rad[NUM_STEPS];
    int rai[NUM_STEPS];
    int wai[NUM_STEPS];
    int ctrl[NUM_STEPS];
    int hasCoeff[NUM_STEPS];
    int isNop[NUM_STEPS];
} DecodedProgram;

/* Engine state */
typedef struct {
    double *memory;
    int memory_size;
    int write_ptr;
    double regs[NUM_REGS];
    double coefficients[16];
    DecodedProgram prog;
    int output_step_l;
    int output_step_r;
    double sr_ratio;
    double sr;
    double lfo_phase;
    double lfo_value;
} WCSState;

static void decode_step(unsigned int word, int idx, DecodedProgram *p)
{
    unsigned char mi31_24 = (word >> 24) & 0xFF;
    unsigned char mi23_16 = (word >> 16) & 0xFF;
    unsigned char mi15_8  = (word >> 8) & 0xFF;
    unsigned char mi7_0   = word & 0xFF;

    p->wai[idx] = mi31_24 & 7;
    p->ctrl[idx] = (mi31_24 >> 3) & 0x1F;
    p->ofst[idx] = (mi15_8 << 8) | mi7_0;
    p->hasCoeff[idx] = (mi23_16 != 0xFF);
    p->isNop[idx] = (mi31_24 == 0xFF && mi23_16 == 0xFF);

    if (p->hasCoeff[idx]) {
        int c8 = (mi23_16 >> 0) & 1;
        int c1 = (mi23_16 >> 1) & 1;
        int c2 = (mi23_16 >> 2) & 1;
        int c3 = (mi23_16 >> 3) & 1;
        p->cCode[idx] = (c8 << 3) | (c3 << 2) | (c2 << 1) | c1;
        p->acc0[idx]  = ((mi23_16 >> 4) & 1);
        p->rad[idx]   = (mi23_16 >> 5) & 3;
        p->rai[idx]   = ((mi23_16 >> 7) & 1);
    } else {
        p->cCode[idx] = 0;
        p->acc0[idx] = 0;
        p->rad[idx] = 0;
        p->rai[idx] = 1;
    }
}

static inline void execute_step(WCSState *st, int s)
{
    DecodedProgram *p = &st->prog;
    if (p->isNop[s]) return;

    int scaled_ofst = (int)(p->ofst[s] * st->sr_ratio + 0.5);

    /* LFO modulation on long delays */
    if (scaled_ofst > (int)(5000 * st->sr_ratio) && st->lfo_value != 0.0) {
        int mod_amount = (int)(st->lfo_value * st->sr_ratio * 1.5);
        scaled_ofst += mod_amount;
    }

    if (scaled_ofst >= st->memory_size) scaled_ofst = st->memory_size - 1;
    if (scaled_ofst < 0) scaled_ofst = 0;

    int read_pos = st->write_ptr - scaled_ofst;
    if (read_pos < 0) read_pos += st->memory_size;

    if (p->hasCoeff[s]) {
        double mul_input;
        if (p->rai[s])
            mul_input = st->memory[read_pos];
        else
            mul_input = st->regs[p->rad[s]];

        double result = mul_input * st->coefficients[p->cCode[s]];

        /* Soft clamp */
        if (result > 4.0) result = 4.0;
        else if (result < -4.0) result = -4.0;

        if (p->acc0[s])
            st->regs[p->wai[s]] += result;
        else
            st->regs[p->wai[s]] = result;

        /* Register clamp */
        if (st->regs[p->wai[s]] > 8.0) st->regs[p->wai[s]] = 8.0;
        else if (st->regs[p->wai[s]] < -8.0) st->regs[p->wai[s]] = -8.0;
    }

    int do_mem_write = (p->ctrl[s] & 0x10) && (p->ctrl[s] != 0x1F);

    if (do_mem_write) {
        double write_val = st->regs[p->wai[s]];
        if (write_val > 1.5 || write_val < -1.5)
            write_val = tanh(write_val * 0.667) * 1.5;
        st->memory[read_pos] = write_val;
    } else if (!p->hasCoeff[s] && p->ctrl[s] != 0x1F) {
        st->regs[p->wai[s]] = st->memory[read_pos];
    }
}

/*
 * Generate a stereo impulse response.
 *
 * microcode: 128 uint32 words for the program
 * coefficients: 16 doubles
 * sr: sample rate
 * n_samples: number of output samples
 * rolloff_hz: lowpass cutoff frequency
 * output_l, output_r: pre-allocated output buffers (n_samples each)
 */
void wcs_generate_ir(const unsigned int *microcode,
                     const double *coefficients,
                     double sr, int n_samples,
                     double rolloff_hz,
                     double *output_l, double *output_r)
{
    WCSState st;
    memset(&st, 0, sizeof(st));

    st.sr = sr;
    st.sr_ratio = sr / 20480.0;
    st.memory_size = (int)(65536.0 * st.sr_ratio) + 16;
    st.memory = (double *)calloc(st.memory_size, sizeof(double));

    /* Decode microcode */
    for (int i = 0; i < NUM_STEPS; i++)
        decode_step(microcode[i], i, &st.prog);

    /* Copy coefficients */
    for (int i = 0; i < 16; i++)
        st.coefficients[i] = coefficients[i];

    /* Find output steps */
    st.output_step_l = 60;
    st.output_step_r = 124;
    for (int i = 0; i < 64; i++) {
        if (st.prog.ctrl[i] == 0x1E && st.prog.wai[i] == 1 && !st.prog.hasCoeff[i]) {
            st.output_step_l = i;
            break;
        }
    }
    for (int i = 64; i < 128; i++) {
        if (st.prog.ctrl[i] == 0x1E && st.prog.wai[i] == 1 && !st.prog.hasCoeff[i]) {
            st.output_step_r = i;
            break;
        }
    }

    /* Rolloff filter */
    double w = exp(-2.0 * M_PI * rolloff_hz / sr);
    double lp_a1 = w;
    double lp_b0 = 1.0 - w;
    double lp_z_l = 0.0, lp_z_r = 0.0;

    /* DC blocker */
    double dc_x1_l = 0.0, dc_y1_l = 0.0;
    double dc_x1_r = 0.0, dc_y1_r = 0.0;

    double input_gain = 0.25;
    double output_gain = 1.0 / input_gain;

    for (int n = 0; n < n_samples; n++) {
        double inp = (n == 0) ? 1.0 : 0.0;

        /* Rolloff */
        lp_z_l = inp * lp_b0 + lp_z_l * lp_a1;
        lp_z_r = inp * lp_b0 + lp_z_r * lp_a1;

        double pd_l = lp_z_l * input_gain;
        double pd_r = lp_z_r * input_gain;

        double captured_l = 0.0, captured_r = 0.0;

        /* Left half */
        st.regs[2] = pd_l;
        for (int s = 0; s < 64; s++) {
            execute_step(&st, s);
            if (s == st.output_step_l)
                captured_l = st.regs[1];
        }

        /* Right half */
        st.regs[2] = pd_r;
        for (int s = 64; s < 128; s++) {
            execute_step(&st, s);
            if (s == st.output_step_r)
                captured_r = st.regs[1];
        }

        /* Advance write pointer */
        st.write_ptr = (st.write_ptr + 1) % st.memory_size;

        /* LFO */
        st.lfo_phase += 0.37 / sr;
        if (st.lfo_phase >= 1.0) st.lfo_phase -= 1.0;
        st.lfo_value = sin(st.lfo_phase * 2.0 * M_PI);

        /* DC blocking */
        double dc_y_l = captured_l - dc_x1_l + 0.9975 * dc_y1_l;
        dc_x1_l = captured_l; dc_y1_l = dc_y_l;

        double dc_y_r = captured_r - dc_x1_r + 0.9975 * dc_y1_r;
        dc_x1_r = captured_r; dc_y1_r = dc_y_r;

        output_l[n] = dc_y_l * output_gain;
        output_r[n] = dc_y_r * output_gain;
    }

    free(st.memory);
}
