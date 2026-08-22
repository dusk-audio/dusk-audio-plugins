// Minimal lilv host for Sunset Circuits LV2 host-integration smoke (Phase 5 QA).
// Built by core/tests/CMakeLists.txt when lilv-0 is discoverable via pkg-config
// (the target is skipped, not failed, when it is not) and run by
// core/tests/run_all.sh against the locally built LV2 bundle.
// It:
//   * instantiates + activates + runs the bundle (via lilv, the library jalv
//     uses), providing the URID map and options features a real host provides;
//   * sweeps the oversampling factor and reads the reported-latency output port
//     (expect 0 / 12 / 14 for 1x / 2x / 4x);
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

// Exit codes: 0 ok, 1 usage, 2 plugin not found, 3 instantiate failed,
// 4 no latency port, 5 no oversampling port, 6 one or more checks FAILED
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
    int oversamplingIdx = -1, latencyIdx = -1, atomInIdx = -1;

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

    lilv_instance_activate(inst);
    activated = 1;

    printf("port count: %u   oversampling idx=%d   latency idx=%d\n",
           nPorts, oversamplingIdx, latencyIdx);
    printf("\n  osParam   factor   reported latency (host samples)   audio finite?   result\n");
    printf("  -------   ------   ------------------------------   -------------   ------\n");

    // The expected PDC values, in host samples, per oversampling setting. These
    // are the halfband decimator group delays asserted in MultiSynthPlugin.cpp
    // (latencyForOsParam): 1x bypasses the filters, 2x is downA alone, 4x is
    // downA + downB. Printing them without checking them (the previous
    // behaviour) meant a PDC regression scrolled past as a green run.
    static const float kWantLatency[3] = { 0.0f, 12.0f, 14.0f };
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
        // host, so no control INPUT port can show it. The reported latency can:
        // every factory preset's baseline sets oversampling to 2x, so parking the
        // host's oversampling port at 4x (latency 14) and then sending a program
        // change must pull the reported latency to the preset's 2x value (12).
        // That is a parameter the preset owns, observed through a real host port.
        uint8_t pc[2] = { 0xC0, 0 };

        ctrl[oversamplingIdx] = 2.0f; // 4x
        lilv_instance_run(inst, BLOCK);
        lilv_instance_run(inst, BLOCK);
        const float latBefore = ctrl[latencyIdx];

        put_midi_event(buf, midiEventURID, pc, 2);   // program 0
        lilv_instance_run(inst, BLOCK);
        clear_seq(buf);
        const float latAfter = ctrl[latencyIdx];

        const int pcOk = (latBefore == 14.0f) && (latAfter == 12.0f);
        printf("program change (0xC0 prog 0): latency %.0f -> %.0f "
               "(want 14 -> 12, the preset's 2x oversampling)  %s\n",
               (double)latBefore, (double)latAfter, pcOk ? "PASS" : "FAIL");
        if (!pcOk) ++failures;

        // Out-of-range program must be ignored, not clamped or crashed into.
        // Re-arming 4x needs the control port to actually CHANGE: the program change
        // moved the plugin's internal oversampling to 2x behind the host's back, so
        // the port still reads 4x and DAF (which only pushes a control port whose
        // value differs from the last one it pushed) would not re-send it. Go via 1x.
        ctrl[oversamplingIdx] = 0.0f; // 1x
        lilv_instance_run(inst, BLOCK);
        ctrl[oversamplingIdx] = 2.0f; // 4x
        lilv_instance_run(inst, BLOCK);
        lilv_instance_run(inst, BLOCK);
        pc[1] = 127;                                  // > 54 factory presets
        put_midi_event(buf, midiEventURID, pc, 2);
        lilv_instance_run(inst, BLOCK);
        clear_seq(buf);
        const float latOor = ctrl[latencyIdx];
        const int oorOk = latOor == 14.0f;
        printf("program change (0xC0 prog 127): latency %.0f "
               "(want 14, out-of-range ignored)  %s\n",
               (double)latOor, oorOk ? "PASS" : "FAIL");
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
