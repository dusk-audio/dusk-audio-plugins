// Minimal lilv host for Sunset Circuits LV2 host-integration smoke (Phase 5 QA).
// Built by core/tests/CMakeLists.txt when lilv-0 is discoverable via pkg-config
// (the target is skipped, not failed, when it is not) and run by
// core/tests/run_all.sh against the locally built LV2 bundle.
// It:
//   * instantiates + activates + runs the bundle (via lilv, the library jalv
//     uses), providing the URID map and options features a real host provides;
//   * sweeps the oversampling factor and reads the reported-latency output port
//     (expect 13 at every factor: the latency is constant, so changing the
//     factor never makes a host restart the plugin);
//   * injects a MIDI note-on and confirms the MIDI-to-audio path is audible;
//   * injects a MIDI program change (0xC0) and confirms it reached loadProgram().
//
// Standalone build (the suite does this for you):
//   cc lv2_smoke.c $(pkg-config --cflags --libs lilv-0) -lm -o lv2_smoke
//   LV2_PATH=<dir-containing-the-.lv2-bundle> ./lv2_smoke <plugin-uri>
// LV2_PATH must contain ONLY the bundle; lilv logs errors on sibling .vst3/.clap.

#include <lilv/lilv.h>
#include <lv2/urid/urid.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/options/options.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/parameters/parameters.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- tiny URID map/unmap (needed by DAF for atom ports) --------------------
#define MAX_URIS 256
static char*    g_uris[MAX_URIS];
static uint32_t g_nUris = 0;

static LV2_URID map_uri(LV2_URID_Map_Handle h, const char* uri) {
    (void)h;
    for (uint32_t i = 0; i < g_nUris; ++i)
        if (strcmp(g_uris[i], uri) == 0) return i + 1;
    if (g_nUris >= MAX_URIS) {
        fprintf(stderr, "map_uri: URI table full (%d), cannot map %s\n", MAX_URIS, uri);
        return 0; // LV2_URID 0 = invalid/failure per the URID spec
    }
    g_uris[g_nUris] = strdup(uri);
    return ++g_nUris;
}
static const char* unmap_uri(LV2_URID_Unmap_Handle h, LV2_URID urid) {
    (void)h;
    return (urid >= 1 && urid <= g_nUris) ? g_uris[urid - 1] : NULL;
}
static void free_uris(void) {
    for (uint32_t i = 0; i < g_nUris; ++i) { free(g_uris[i]); g_uris[i] = NULL; }
    g_nUris = 0;
}

#define BLOCK 512

// Overwrite an input atom port's sequence with exactly one MIDI event at frame 0.
static void put_midi_event(uint8_t* buf, LV2_URID midiEventURID,
                           const uint8_t* msg, uint32_t size) {
    LV2_Atom_Sequence* seq = (LV2_Atom_Sequence*)buf;
    seq->atom.type = map_uri(NULL, LV2_ATOM__Sequence);
    seq->body.unit = 0;
    seq->body.pad  = 0;
    LV2_Atom_Event* ev = (LV2_Atom_Event*)(buf + sizeof(LV2_Atom_Sequence));
    ev->time.frames = 0;
    ev->body.type = midiEventURID;
    ev->body.size = size;
    memcpy((uint8_t*)(ev + 1), msg, size);
    seq->atom.size = sizeof(LV2_Atom_Sequence_Body)
                   + lv2_atom_pad_size(sizeof(LV2_Atom_Event) + size);
}

// Empty the sequence again (the host clears the input buffer between cycles).
static void clear_seq(uint8_t* buf) {
    ((LV2_Atom_Sequence*)buf)->atom.size = sizeof(LV2_Atom_Sequence_Body);
}

// Run `blocks` blocks and return the peak absolute sample over every audio port,
// or NAN if any sample is not finite. A NaN compares false against everything, so
// the peak alone let one through every check below; a NAN result fails them all.
static float run_peak(LilvInstance* inst, float** audio, uint32_t nPorts, int blocks) {
    float peak = 0.0f;
    int finite = 1;
    for (int b = 0; b < blocks; ++b) {
        lilv_instance_run(inst, BLOCK);
        for (uint32_t i = 0; i < nPorts; ++i)
            if (audio[i])
                for (int k = 0; k < BLOCK; ++k) {
                    if (!isfinite(audio[i][k])) finite = 0;
                    const float a = fabsf(audio[i][k]);
                    if (a > peak) peak = a;
                }
    }
    return finite ? peak : NAN;
}

// Exit codes: 0 ok, 1 usage, 2 plugin not found, 3 instantiate failed,
// 4 no latency port, 5 no oversampling or masterVol port, 6 one or more checks FAILED
// (including a missing MIDI input port, which is a broken bundle, not a
// reason to skip the MIDI section).
int main(int argc, char** argv) {
    // Everything that needs releasing is declared up front and NULL, so every
    // exit path can funnel through the single `cleanup:` label below. This is a
    // test fixture, but it must run clean under ASan/valgrind — a leak here is
    // indistinguishable from a leak in the plugin under test.
    int rc = 0, failures = 0, activated = 0;
    LilvWorld*    world = NULL;
    LilvInstance* inst  = NULL;
    LilvNode *uriNode = NULL, *nm = NULL;
    LilvNode *lv2Input = NULL, *lv2Output = NULL, *lv2Audio = NULL,
             *lv2Control = NULL, *atomPort = NULL;
    float  *ctrl = NULL, *mins = NULL, *maxs = NULL, *defs = NULL;
    float  **audio = NULL;
    uint8_t **atomBuf = NULL;
    uint32_t nPorts = 0;
    const LilvPlugins* plugins = NULL;
    const LilvPlugin*  plug = NULL;
    int oversamplingIdx = -1, latencyIdx = -1, masterVolIdx = -1, atomInIdx = -1;

    if (argc < 2) { fprintf(stderr, "usage: lv2_smoke <plugin-uri>\n"); return 1; }
    const char* uri = argv[1];

    world = lilv_world_new();
    // NOTE: under ASan this call leaks exactly one 24-byte LilvNode inside
    // lilv_world_load_plugin_classes -> lilv_world_add_plugin_class, which
    // lilv_world_free does not release. That one allocation is upstream lilv's
    // and is the ONLY thing this fixture reports (498680 bytes in 7015
    // allocations before the cleanup path below existed). Do not chase it.
    lilv_world_load_all(world);
    plugins = lilv_world_get_all_plugins(world);

    uriNode = lilv_new_uri(world, uri);
    plug = lilv_plugins_get_by_uri(plugins, uriNode);
    if (!plug) { fprintf(stderr, "plugin not found: %s\n", uri); rc = 2; goto cleanup; }

    nm = lilv_plugin_get_name(plug);
    printf("plugin: %s\n", lilv_node_as_string(nm));
    lilv_node_free(nm);
    nm = NULL;

    LV2_URID_Map   map   = { NULL, map_uri };
    LV2_URID_Unmap unmap = { NULL, unmap_uri };
    LV2_Feature mapF   = { LV2_URID__map,   &map };
    LV2_Feature unmapF = { LV2_URID__unmap, &unmap };

    // Options feature (DAF refuses to instantiate without it): block-length
    // bounds and sample rate.
    const int32_t blockLen = BLOCK;
    const float   srate    = 48000.0f;
    const LV2_URID uMax = map_uri(NULL, LV2_BUF_SIZE__maxBlockLength);
    const LV2_URID uMin = map_uri(NULL, LV2_BUF_SIZE__minBlockLength);
    const LV2_URID uNom = map_uri(NULL, LV2_BUF_SIZE__nominalBlockLength);
    const LV2_URID uSr  = map_uri(NULL, LV2_PARAMETERS__sampleRate);
    const LV2_URID uInt = map_uri(NULL, LV2_ATOM__Int);
    const LV2_URID uFlt = map_uri(NULL, LV2_ATOM__Float);
    LV2_Options_Option options[] = {
        { LV2_OPTIONS_INSTANCE, 0, uMax, sizeof(int32_t), uInt, &blockLen },
        { LV2_OPTIONS_INSTANCE, 0, uMin, sizeof(int32_t), uInt, &blockLen },
        { LV2_OPTIONS_INSTANCE, 0, uNom, sizeof(int32_t), uInt, &blockLen },
        { LV2_OPTIONS_INSTANCE, 0, uSr,  sizeof(float),   uFlt, &srate },
        { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, NULL },
    };
    LV2_Feature optF = { LV2_OPTIONS__options, options };
    const LV2_Feature* features[] = { &mapF, &unmapF, &optF, NULL };

    nPorts = lilv_plugin_get_num_ports(plug);

    // Port class URIs.
    lv2Input   = lilv_new_uri(world, LV2_CORE__InputPort);
    lv2Output  = lilv_new_uri(world, LV2_CORE__OutputPort);
    lv2Audio   = lilv_new_uri(world, LV2_CORE__AudioPort);
    lv2Control = lilv_new_uri(world, LV2_CORE__ControlPort);
    atomPort   = lilv_new_uri(world, LV2_ATOM__AtomPort);

    // Buffers.
    ctrl  = calloc(nPorts, sizeof(float)); // one control slot per port index
    audio = calloc(nPorts, sizeof(float*));
    // Atom buffers (one 4 KB slot per atom port), indexed by REAL port index —
    // this plugin has 200+ ports, so a fixed 64-slot table masked with (i & 63)
    // could alias two atom ports onto one buffer.
    atomBuf = calloc(nPorts, sizeof(uint8_t*));

    // Fill control defaults.
    mins = calloc(nPorts, sizeof(float));
    maxs = calloc(nPorts, sizeof(float));
    defs = calloc(nPorts, sizeof(float));
    if (!ctrl || !audio || !atomBuf || !mins || !maxs || !defs) {
        fprintf(stderr, "out of memory allocating port tables\n"); rc = 3; goto cleanup;
    }
    lilv_plugin_get_port_ranges_float(plug, mins, maxs, defs);

    const double sampleRate = 48000.0;
    inst = lilv_plugin_instantiate(plug, sampleRate, features);
    if (!inst) { fprintf(stderr, "instantiate failed\n"); rc = 3; goto cleanup; }

    for (uint32_t i = 0; i < nPorts; ++i) {
        const LilvPort* port = lilv_plugin_get_port_by_index(plug, i);
        const int isInput   = lilv_port_is_a(plug, port, lv2Input);
        const int isAudio   = lilv_port_is_a(plug, port, lv2Audio);
        const int isControl = lilv_port_is_a(plug, port, lv2Control);
        const int isAtom    = lilv_port_is_a(plug, port, atomPort);
        const LilvNode* sym = lilv_port_get_symbol(plug, port);
        const char* s = lilv_node_as_string(sym);

        if (isAudio) {
            audio[i] = calloc(BLOCK, sizeof(float));
            lilv_instance_connect_port(inst, i, audio[i]);
        } else if (isControl) {
            ctrl[i] = isfinite(defs[i]) ? defs[i] : 0.0f;
            lilv_instance_connect_port(inst, i, &ctrl[i]);
            if (strcmp(s, "oversampling") == 0) oversamplingIdx = (int)i;
            if (strcmp(s, "lv2_latency") == 0)  latencyIdx = (int)i;
            if (strcmp(s, "masterVol") == 0)    masterVolIdx = (int)i;
        } else if (isAtom) {
            uint8_t* buf = calloc(4096, 1);
            atomBuf[i] = buf;
            LV2_Atom_Sequence* seq = (LV2_Atom_Sequence*)buf;
            seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
            seq->atom.type = map_uri(NULL, LV2_ATOM__Sequence);
            seq->body.unit = 0;
            seq->body.pad  = 0;
            if (isInput) {
                // input atom: capacity in size for host->plugin; DAF reads it
                seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
                if (atomInIdx < 0) atomInIdx = (int)i;
            }
            lilv_instance_connect_port(inst, i, buf);
        } else {
            // CV or unknown: give it a control slot to be safe.
            lilv_instance_connect_port(inst, i, &ctrl[i]);
        }
        (void)isInput;
    }

    if (latencyIdx < 0)      { fprintf(stderr, "no latency port found\n"); rc = 4; goto cleanup; }
    if (oversamplingIdx < 0) { fprintf(stderr, "no oversampling port found\n"); rc = 5; goto cleanup; }
    if (masterVolIdx < 0)    { fprintf(stderr, "no masterVol port found\n"); rc = 5; goto cleanup; }

    lilv_instance_activate(inst);
    activated = 1;

    printf("port count: %u   oversampling idx=%d   latency idx=%d\n",
           nPorts, oversamplingIdx, latencyIdx);
    printf("\n  osParam   factor   reported latency (host samples)   audio finite?   result\n");
    printf("  -------   ------   ------------------------------   -------------   ------\n");

    // The expected PDC, in host samples, per oversampling setting: the same 13 at
    // every factor. The engine pads the voice path (1x by 13, 2x by 2, 4x not at
    // all) so the report never moves and a factor change never makes the host
    // restart the plugin. Printing these without checking them (an earlier
    // behaviour) let a PDC regression scroll past as a green run.
    static const float kWantLatency[3] = { 13.0f, 13.0f, 13.0f };
    const char* facName[3] = { "1x", "2x", "4x" };
    for (int os = 0; os <= 2; ++os) {
        ctrl[oversamplingIdx] = (float)os;
        // Run two blocks: the first applies the param + re-prepares; latency is
        // written on run().
        lilv_instance_run(inst, BLOCK);
        lilv_instance_run(inst, BLOCK);
        const float lat = ctrl[latencyIdx];
        int finite = 1;
        for (uint32_t i = 0; i < nPorts && finite; ++i)
            if (audio[i])
                for (int k = 0; k < BLOCK; ++k)
                    if (!isfinite(audio[i][k])) { finite = 0; break; }
        const int latOk = (lat == kWantLatency[os]);
        const int rowOk = latOk && finite;
        printf("  %6.0f    %4s     %28.0f   %13s   %s\n", (double)ctrl[oversamplingIdx],
               facName[os], (double)lat, finite ? "yes" : "NO", rowOk ? "PASS" : "FAIL");
        if (!latOk) {
            fprintf(stderr, "FAIL: %s reported latency %.0f, want %.0f\n",
                    facName[os], (double)lat, (double)kWantLatency[os]);
            ++failures;
        }
        if (!finite) {
            fprintf(stderr, "FAIL: %s produced non-finite audio (NaN/Inf)\n", facName[os]);
            ++failures;
        }
    }

    // A bundle with no MIDI input port cannot be an instrument. Skipping the
    // MIDI section here (the previous behaviour) turned the most important
    // half of this smoke test into a silent no-op that still exited 0.
    if (atomInIdx < 0) {
        fprintf(stderr, "FAIL: no input atom port found — this is an instrument, "
                        "the MIDI input port is mandatory\n");
        rc = 6;
        goto cleanup;
    }

    // --- MIDI -> audio smoke: inject a note-on and confirm non-silent output.
    {
        ctrl[oversamplingIdx] = 1.0f; // 2x
        // Prime the oversampling change with one empty run() so the engine's
        // re-preparation and latency update settle BEFORE the note is injected;
        // injecting into the same block as a factor switch could race the
        // re-prepare and eat the note (false SILENT fail).
        lilv_instance_run(inst, BLOCK);
        const LV2_URID midiEventURID = map_uri(NULL, "http://lv2plug.in/ns/ext/midi#MidiEvent");
        uint8_t* buf = atomBuf[atomInIdx];
        const uint8_t noteOn[3] = { 0x90, 60, 100 }; // note-on C4 vel 100
        put_midi_event(buf, midiEventURID, noteOn, 3);

        float peak = 0.0f;
        for (int b = 0; b < 20; ++b) {           // ~213 ms of audio
            lilv_instance_run(inst, BLOCK);
            for (uint32_t i = 0; i < nPorts; ++i)
                if (audio[i])
                    for (int k = 0; k < BLOCK; ++k) {
                        const float a = fabsf(audio[i][k]);
                        if (a > peak) peak = a;
                    }
            // After the first run the host would normally clear the input
            // sequence; DAF has already consumed the note, so leave it.
            clear_seq(buf);
        }
        const int audible = peak > 1e-4f;
        printf("\nMIDI->audio smoke (note-on C4 @ 2x): peak = %.4f (%s)\n",
               peak, audible ? "AUDIBLE - PASS" : "SILENT - FAIL");
        if (!audible) ++failures;

        // --- MIDI program change (0xC0) -> loadProgram().
        // The plugin applies a self-initiated program change without telling the
        // host, so no control INPUT port can show it. The audio can: park the
        // host's Master Volume port at -60 dB, then send program 0. Its rows
        // rewrite Master Volume to the preset's own level behind the host's back,
        // so the output must come up by more than 30 dB. A fresh note after the
        // change keeps this independent of what the preset does with held notes.
        // (This used to watch the reported latency follow the preset's 2x
        // oversampling; the latency no longer moves.)
        uint8_t pc[2] = { 0xC0, 0 };
        const uint8_t noteOn2[3] = { 0x90, 64, 100 };  // E4

        ctrl[masterVolIdx] = -60.0f;
        (void)run_peak(inst, audio, nPorts, 8);   // let the 8 ms volume glide settle
        const float quiet = run_peak(inst, audio, nPorts, 24);

        // put_midi_event() writes the sequence's only event, so the program change
        // and the note go in separate blocks.
        put_midi_event(buf, midiEventURID, pc, 2);   // program 0
        const float pcBlock = run_peak(inst, audio, nPorts, 1);
        clear_seq(buf);
        put_midi_event(buf, midiEventURID, noteOn2, 3);
        const float noteBlock = run_peak(inst, audio, nPorts, 1);
        clear_seq(buf);
        const float rest = run_peak(inst, audio, nPorts, 47);
        // fmaxf() drops a NaN operand, so a non-finite block is carried explicitly.
        const float loud = (isnan(noteBlock) || isnan(rest)) ? NAN : fmaxf(noteBlock, rest);
        const float latPc = ctrl[latencyIdx];

        const int pcOk = !isnan(pcBlock) && loud > 1.0e-3f && loud > quiet * 31.6f
                      && latPc == 13.0f;
        printf("program change (0xC0 prog 0): peak %.5f -> %.5f (want > +30 dB, the "
               "preset's own Master Volume), latency %.0f (want 13)  %s\n",
               (double)quiet, (double)loud, (double)latPc, pcOk ? "PASS" : "FAIL");
        if (!pcOk) ++failures;

        // Out-of-range program must be ignored, not clamped or crashed into. Re-park
        // Master Volume first; the port has to CHANGE for DAF to push it again
        // (program 0 moved the internal value behind the host's back while the port
        // still reads -60, and DAF only pushes a port whose value differs from the
        // last one it pushed), hence -59.9. The windows span two arp steps of the
        // preset's pattern, so the two peaks compare like with like. Ignored means
        // the same pattern plays on at the same level: within 10 dB either way. A
        // one-sided bound passed silence after the change.
        ctrl[masterVolIdx] = -59.9f;
        (void)run_peak(inst, audio, nPorts, 8);   // settle, as above
        const float parked = run_peak(inst, audio, nPorts, 48);
        pc[1] = 127;                                  // > 54 factory presets
        put_midi_event(buf, midiEventURID, pc, 2);
        const float oorBlock = run_peak(inst, audio, nPorts, 1);
        clear_seq(buf);
        const float afterOor = run_peak(inst, audio, nPorts, 48);
        const int oorOk = !isnan(oorBlock) && parked > 0.0f
                       && afterOor > parked / 3.16f && afterOor < parked * 3.16f;
        printf("program change (0xC0 prog 127): peak %.5f -> %.5f "
               "(want within 10 dB: out-of-range ignored)  %s\n",
               (double)parked, (double)afterOor, oorOk ? "PASS" : "FAIL");
        if (!oorOk) ++failures;
    }

    if (failures == 0)
        printf("\nOK: instantiate + activate + run + latency + MIDI checks succeeded.\n");
    else
        printf("\nFAILED: %d check(s) did not pass.\n", failures);
    rc = (failures == 0) ? 0 : 6;

cleanup:
    // Single release path for every exit above. All handles start NULL and the
    // lilv_*_free / free() calls are all NULL-tolerant, so this is safe however
    // early we bailed.
    if (inst) {
        if (activated) lilv_instance_deactivate(inst);
        lilv_instance_free(inst);
    }
    if (audio)   { for (uint32_t i = 0; i < nPorts; ++i) free(audio[i]);   free(audio); }
    if (atomBuf) { for (uint32_t i = 0; i < nPorts; ++i) free(atomBuf[i]); free(atomBuf); }
    free(ctrl);
    free(mins);
    free(maxs);
    free(defs);
    lilv_node_free(nm);
    lilv_node_free(uriNode);
    lilv_node_free(lv2Input);
    lilv_node_free(lv2Output);
    lilv_node_free(lv2Audio);
    lilv_node_free(lv2Control);
    lilv_node_free(atomPort);
    lilv_world_free(world);
    free_uris();   // the map_uri strdup table
    return rc;
}
