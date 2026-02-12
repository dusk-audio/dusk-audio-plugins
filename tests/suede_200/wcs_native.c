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
#include <stdio.h>

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
    double accumulator;  /* Separate hardware accumulator (LS374, clocked by BCON1/) */
    double coefficients[16];
    DecodedProgram prog;
    int output_step_l;
    int output_step_r;
    double sr_ratio;
    double sr;
    double lfo_phase;
    double lfo_value;
    double damping;  /* Per-write decay factor [0.9999..1.0], models 16-bit roundtrip loss */
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

/* 16-bit Q15 quantization: models the Lexicon 200's 16-bit fixed-point
 * signal path. All multiplies, accumulations, and memory stores truncate
 * to 16-bit resolution, introducing natural quantization noise and
 * preventing infinite-precision feedback accumulation. */
static inline double q15(double x)
{
    /* Q15 format: 1 sign bit + 15 fractional bits. LSB = 1/32768. */
    return floor(x * 32768.0 + 0.5) / 32768.0;
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

    /* ── Non-coefficient steps: delay tap routing and I/O ──
     *
     * v4 hybrid routing based on M200 ARU hardware analysis:
     *
     * MWR+MCEN/ combo (I/O injection/extraction nodes):
     *   Load accumulator with old memory value BEFORE overwriting.
     *   This seeds the accumulator with channel-specific delay energy,
     *   critical for stereo decorrelation in Concert Hall.
     *
     * MCEN/ only (pure tap reads):
     *   Route to register file only — does NOT touch accumulator.
     *   Prevents reset cascade in programs with dense tap reads (Plate).
     *
     * MWR only (write-back without read):
     *   Write register to memory, no accumulator interaction.
     */
    if (!p->hasCoeff[s]) {
        if (p->ctrl[s] == 0x1F)
            return; /* NOP */

        if (p->ctrl[s] & 0x10) {
            /* MWR: write register to delay memory */
            /* If MCEN/ also set, capture old memory into accumulator first */
            if (p->ctrl[s] & 0x08)
                st->accumulator = st->memory[read_pos];

            double write_val = st->regs[p->wai[s]];
            if (write_val > 1.0) write_val = 1.0;
            else if (write_val < -1.0) write_val = -1.0;
            st->memory[read_pos] = q15(write_val);
        } else if (p->ctrl[s] & 0x08) {
            /* MCEN/ only: pure tap read → register file only */
            st->regs[p->wai[s]] = st->memory[read_pos];
        }
        return;
    }

    /* ── Coefficient steps: split accumulator architecture ──
     *
     * The Lexicon 200 ARU has THREE separate data stores:
     *   1. Register file (LS670 x4): 4-location dual-port RAM
     *      - Write enabled by BCON3 (AREG/ signal)
     *   2. Accumulator (LS374): single 16-bit latch
     *      - Clocked by BCON1/ (and BCON2/ for output chain)
     *   3. Transfer register (LS374): captures ALU output for memory writes
     *      - Clocked by MWR/
     *
     * BCON (Bus Control, 74LS139 decoded):
     *   BCON=0: neither accumulator nor register file updated
     *   BCON=1: accumulator latches ALU output (BCON1/ active)
     *   BCON=2: accumulator latches ALU output (output chain)
     *   BCON=3: register file stores ALU output (BCON3/AREG/ active)
     *
     * ACC0 controls ALU operation:
     *   ACC0=0: ALU output = multiply result (load)
     *   ACC0=1: ALU output = accumulator + multiply result (accumulate)
     */
    double mul_input;
    if (p->rai[s])
        mul_input = st->memory[read_pos];
    else
        mul_input = st->regs[p->rad[s]];

    double result = mul_input * st->coefficients[p->cCode[s]];

    /* OP/ (ctrl bit 2 = MI29): negate the multiply result. */
    if (p->ctrl[s] & 0x04)
        result = -result;

    result = q15(result);
    result *= st->damping;

    /* ALU operation: accumulate or load */
    double alu_out;
    if (p->acc0[s])
        alu_out = st->accumulator + result;
    else
        alu_out = result;

    /* BCON routing — determines which data store receives ALU output.
     *
     * From M200 schematics (T&C board, 74LS139 U6):
     *   BCON=0: Y0/ NOT routed to ARU — true no-op (compute and discard)
     *   BCON=1: SC/ → accumulator latch (BCON1/)
     *   BCON=2: BCON2/ → accumulator latch + overload detect
     *   BCON=3: AREG/ → register file write enable
     *
     * The transfer register (MWR/) is independent of BCON and always
     * captures ALU output when MWR is set, regardless of BCON value.
     */
    int bcon = p->ctrl[s] & 0x03;
    if (bcon == 3) {
        /* Register file write (BCON3/AREG/ active) */
        st->regs[p->wai[s]] = alu_out;
        if (st->regs[p->wai[s]] > 4.0) st->regs[p->wai[s]] = 4.0;
        else if (st->regs[p->wai[s]] < -4.0) st->regs[p->wai[s]] = -4.0;
    } else if (bcon != 0) {
        /* Accumulator latch (BCON=1,2 only) */
        st->accumulator = alu_out;
        if (st->accumulator > 4.0) st->accumulator = 4.0;
        else if (st->accumulator < -4.0) st->accumulator = -4.0;
    }
    /* BCON=0: ALU output discarded — neither acc nor reg file updated */

    /* Memory write (MWR / ctrl bit 4): transfer register captures ALU output.
     *
     * From M200 schematics (ARU board):
     *   Transfer register (LS374, U22/U23) is clocked by XCLK/ = MWR/.
     *   It captures the ALU output (alu_out), which is the accumulated
     *   sum when ACC0=1, or the multiply result when ACC0=0.
     *   This is independent of BCON routing — MWR can fire on any BCON value.
     *
     * This is critical for FDN topology: multi-step accumulation chains
     * (e.g., sum of delay taps weighted by coefficients) are written back
     * to delay memory as the accumulated value, not individual products. */
    if ((p->ctrl[s] & 0x10) && (p->ctrl[s] != 0x1F)) {
        double write_val = alu_out;  /* transfer register captures ALU output */
        if (write_val > 1.0) write_val = 1.0;
        else if (write_val < -1.0) write_val = -1.0;
        st->memory[read_pos] = q15(write_val);
    }
}

/*
 * Initialize a WCSState with decoded microcode and coefficients.
 */
static void wcs_init(WCSState *st, const unsigned int *microcode,
                     const double *coefficients, double sr, double damping)
{
    memset(st, 0, sizeof(*st));
    st->sr = sr;
    st->sr_ratio = sr / 24000.0;  /* M200: 18.432 MHz / 6 / 128 = 24 kHz */
    st->damping = (damping > 0.0 && damping <= 1.0) ? damping : 1.0;
    st->memory_size = (int)(65536.0 * st->sr_ratio) + 16;
    st->memory = (double *)calloc(st->memory_size, sizeof(double));

    for (int i = 0; i < NUM_STEPS; i++)
        decode_step(microcode[i], i, &st->prog);
    for (int i = 0; i < 16; i++)
        st->coefficients[i] = coefficients[i];

    st->output_step_l = 60;
    st->output_step_r = 124;
    for (int i = 0; i < 64; i++) {
        if (st->prog.ctrl[i] == 0x1E && st->prog.wai[i] == 1 && !st->prog.hasCoeff[i]) {
            st->output_step_l = i;
            break;
        }
    }
    for (int i = 64; i < 128; i++) {
        if (st->prog.ctrl[i] == 0x1E && st->prog.wai[i] == 1 && !st->prog.hasCoeff[i]) {
            st->output_step_r = i;
            break;
        }
    }
}

/*
 * Generate a stereo impulse response (cold-start).
 *
 * NOTE: Due to the WCS engine's 3.2-second buffer revolution, cold-start
 * impulse responses have unrealistically long pre-delays. For optimization
 * and realistic IR capture, use wcs_process_signal with ESS deconvolution.
 *
 * inject_gain: if > 0, seeds the entire delay memory at sample 0 to provide
 *              immediate energy to FDN coefficient steps (crude approximation).
 */
void wcs_generate_ir(const unsigned int *microcode,
                     const double *coefficients,
                     double sr, int n_samples,
                     double rolloff_hz,
                     double inject_gain,
                     double damping,
                     double *output_l, double *output_r)
{
    if (microcode == NULL || coefficients == NULL ||
        output_l == NULL || output_r == NULL ||
        sr <= 0.0 || n_samples <= 0) {
        return;
    }

    WCSState st;
    wcs_init(&st, microcode, coefficients, sr, damping);
    if (st.memory == NULL) {
        /* Allocation failed; zero outputs and return */
        memset(output_l, 0, n_samples * sizeof(double));
        memset(output_r, 0, n_samples * sizeof(double));
        return;
    }

    double w = exp(-2.0 * M_PI * rolloff_hz / sr);
    double lp_a1 = w;
    double lp_b0 = 1.0 - w;
    double lp_z_l = 0.0, lp_z_r = 0.0;

    double dc_x1_l = 0.0, dc_y1_l = 0.0;
    double dc_x1_r = 0.0, dc_y1_r = 0.0;

    double input_gain = 0.25;
    double output_gain = 1.0 / input_gain;

    for (int n = 0; n < n_samples; n++) {
        double inp = (n == 0) ? 1.0 : 0.0;

        lp_z_l = inp * lp_b0 + lp_z_l * lp_a1;
        lp_z_r = inp * lp_b0 + lp_z_r * lp_a1;

        double pd_l = lp_z_l * input_gain;
        double pd_r = lp_z_r * input_gain;

        /* Optional memory seeding at sample 0 */
        if (n == 0 && inject_gain != 0.0) {
            double seed = pd_l * inject_gain;
            for (int i = 0; i < st.memory_size; i++)
                st.memory[i] = seed;
        }

        double captured_l = 0.0, captured_r = 0.0;

        /* Left channel: reset accumulator before each 64-step block */
        st.accumulator = 0.0;
        st.regs[2] = pd_l;
        for (int s = 0; s < 64; s++) {
            execute_step(&st, s);
            if (s == st.output_step_l)
                captured_l = st.regs[1];
        }

        /* Right channel: reset accumulator */
        st.accumulator = 0.0;
        st.regs[2] = pd_r;
        for (int s = 64; s < 128; s++) {
            execute_step(&st, s);
            if (s == st.output_step_r)
                captured_r = st.regs[1];
        }

        st.write_ptr = (st.write_ptr + 1) % st.memory_size;

        st.lfo_phase += 0.37 / sr;
        if (st.lfo_phase >= 1.0) st.lfo_phase -= 1.0;
        st.lfo_value = sin(st.lfo_phase * 2.0 * M_PI);

        double dc_y_l = captured_l - dc_x1_l + 0.9975 * dc_y1_l;
        dc_x1_l = captured_l; dc_y1_l = dc_y_l;
        double dc_y_r = captured_r - dc_x1_r + 0.9975 * dc_y1_r;
        dc_x1_r = captured_r; dc_y1_r = dc_y_r;

        output_l[n] = dc_y_l * output_gain;
        output_r[n] = dc_y_r * output_gain;
    }

    free(st.memory);
}

/*
 * Process an arbitrary stereo input signal through the WCS engine.
 * Used for ESS (exponential sine sweep) deconvolution-based IR capture.
 *
 * microcode: 128 uint32 words for the program
 * coefficients: 16 doubles
 * sr: sample rate
 * n_samples: number of samples
 * rolloff_hz: lowpass cutoff frequency
 * input_l, input_r: input signal buffers (n_samples each)
 * output_l, output_r: pre-allocated output buffers (n_samples each)
 */
void wcs_process_signal(const unsigned int *microcode,
                        const double *coefficients,
                        double sr, int n_samples,
                        double rolloff_hz,
                        double damping,
                        const double *input_l, const double *input_r,
                        double *output_l, double *output_r)
{
    if (microcode == NULL || coefficients == NULL ||
        input_l == NULL || input_r == NULL ||
        output_l == NULL || output_r == NULL ||
        sr <= 0.0 || n_samples <= 0) {
        return;
    }

    WCSState st;
    wcs_init(&st, microcode, coefficients, sr, damping);

    double w = exp(-2.0 * M_PI * rolloff_hz / sr);
    double lp_a1 = w;
    double lp_b0 = 1.0 - w;
    double lp_z_l = 0.0, lp_z_r = 0.0;

    double dc_x1_l = 0.0, dc_y1_l = 0.0;
    double dc_x1_r = 0.0, dc_y1_r = 0.0;

    double input_gain = 0.25;
    double output_gain = 1.0 / input_gain;

    for (int n = 0; n < n_samples; n++) {
        double inp_l = input_l[n];
        double inp_r = input_r[n];

        lp_z_l = inp_l * lp_b0 + lp_z_l * lp_a1;
        lp_z_r = inp_r * lp_b0 + lp_z_r * lp_a1;

        double pd_l = lp_z_l * input_gain;
        double pd_r = lp_z_r * input_gain;

        double captured_l = 0.0, captured_r = 0.0;

        /* Left channel: reset accumulator */
        st.accumulator = 0.0;
        st.regs[2] = pd_l;
        for (int s = 0; s < 64; s++) {
            execute_step(&st, s);
            if (s == st.output_step_l)
                captured_l = st.regs[1];
        }

        /* Right channel: reset accumulator */
        st.accumulator = 0.0;
        st.regs[2] = pd_r;
        for (int s = 64; s < 128; s++) {
            execute_step(&st, s);
            if (s == st.output_step_r)
                captured_r = st.regs[1];
        }

        st.write_ptr = (st.write_ptr + 1) % st.memory_size;

        st.lfo_phase += 0.37 / sr;
        if (st.lfo_phase >= 1.0) st.lfo_phase -= 1.0;
        st.lfo_value = sin(st.lfo_phase * 2.0 * M_PI);

        double dc_y_l = captured_l - dc_x1_l + 0.9975 * dc_y1_l;
        dc_x1_l = captured_l; dc_y1_l = dc_y_l;

        double dc_y_r = captured_r - dc_x1_r + 0.9975 * dc_y1_r;
        dc_x1_r = captured_r; dc_y1_r = dc_y_r;

        output_l[n] = dc_y_l * output_gain;
        output_r[n] = dc_y_r * output_gain;
    }

    free(st.memory);
}
