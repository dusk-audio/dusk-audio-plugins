// Minimal lilv host for Sunset Circuits LV2 host-integration smoke (Phase 5 QA).
// Standalone; not part of the CMake build. Requires lilv-0 dev headers.
// It:
//   * instantiates + activates + runs the bundle (via lilv, the library jalv
//     uses), providing the URID map and options features a real host provides;
//   * sweeps the oversampling factor and reads the reported-latency output port
//     (expect 0 / 12 / 14 for 1x / 2x / 4x);
//   * injects a MIDI note-on and confirms the MIDI-to-audio path is audible.
//
//   cc lv2_smoke.c $(pkg-config --cflags --libs lilv-0) -lm -o lv2_smoke
//   LV2_PATH=<dir-containing-the-.lv2-bundle> ./lv2_smoke <plugin-uri>

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

// --- tiny URID map/unmap (needed by DPF for atom ports) --------------------
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

#define BLOCK 512

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: lv2_smoke <plugin-uri>\n"); return 1; }
    const char* uri = argv[1];

    LilvWorld* world = lilv_world_new();
    lilv_world_load_all(world);
    const LilvPlugins* plugins = lilv_world_get_all_plugins(world);

    LilvNode* uriNode = lilv_new_uri(world, uri);
    const LilvPlugin* plug = lilv_plugins_get_by_uri(plugins, uriNode);
    if (!plug) { fprintf(stderr, "plugin not found: %s\n", uri); return 2; }

    LilvNode* nm = lilv_plugin_get_name(plug);
    printf("plugin: %s\n", lilv_node_as_string(nm));
    lilv_node_free(nm);

    LV2_URID_Map   map   = { NULL, map_uri };
    LV2_URID_Unmap unmap = { NULL, unmap_uri };
    LV2_Feature mapF   = { LV2_URID__map,   &map };
    LV2_Feature unmapF = { LV2_URID__unmap, &unmap };

    // Options feature (DPF refuses to instantiate without it): block-length
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

    const uint32_t nPorts = lilv_plugin_get_num_ports(plug);

    // Port class URIs.
    LilvNode* lv2Input   = lilv_new_uri(world, LV2_CORE__InputPort);
    LilvNode* lv2Output  = lilv_new_uri(world, LV2_CORE__OutputPort);
    LilvNode* lv2Audio   = lilv_new_uri(world, LV2_CORE__AudioPort);
    LilvNode* lv2Control = lilv_new_uri(world, LV2_CORE__ControlPort);
    LilvNode* atomPort   = lilv_new_uri(world, LV2_ATOM__AtomPort);

    // Buffers.
    float*  ctrl  = calloc(nPorts, sizeof(float)); // one control slot per port index
    float** audio = calloc(nPorts, sizeof(float*));
    // Atom buffers (one 4 KB slot per atom port), initialised as empty sequences.
    uint8_t* atomBuf[64] = {0};

    int oversamplingIdx = -1, latencyIdx = -1, atomInIdx = -1;
    LilvNode* defsNode = NULL;

    // Fill control defaults.
    float* mins = calloc(nPorts, sizeof(float));
    float* maxs = calloc(nPorts, sizeof(float));
    float* defs = calloc(nPorts, sizeof(float));
    lilv_plugin_get_port_ranges_float(plug, mins, maxs, defs);

    const double sampleRate = 48000.0;
    LilvInstance* inst = lilv_plugin_instantiate(plug, sampleRate, features);
    if (!inst) { fprintf(stderr, "instantiate failed\n"); return 3; }

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
            atomBuf[i & 63] = buf;
            LV2_Atom_Sequence* seq = (LV2_Atom_Sequence*)buf;
            seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
            seq->atom.type = map_uri(NULL, LV2_ATOM__Sequence);
            seq->body.unit = 0;
            seq->body.pad  = 0;
            if (isInput) {
                // input atom: capacity in size for host->plugin; DPF reads it
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

    if (latencyIdx < 0)      { fprintf(stderr, "no latency port found\n"); return 4; }
    if (oversamplingIdx < 0) { fprintf(stderr, "no oversampling port found\n"); return 5; }

    lilv_instance_activate(inst);

    printf("port count: %u   oversampling idx=%d   latency idx=%d\n",
           nPorts, oversamplingIdx, latencyIdx);
    printf("\n  osParam   factor   reported latency (host samples)   audio finite?\n");
    printf("  -------   ------   ------------------------------   -------------\n");

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
        printf("  %6.0f    %4s     %28.0f   %13s\n", (double)ctrl[oversamplingIdx],
               facName[os], (double)lat, finite ? "yes" : "NO");
    }

    // --- MIDI -> audio smoke: inject a note-on and confirm non-silent output.
    if (atomInIdx >= 0) {
        ctrl[oversamplingIdx] = 1.0f; // 2x
        const LV2_URID midiEventURID = map_uri(NULL, "http://lv2plug.in/ns/ext/midi#MidiEvent");
        uint8_t* buf = atomBuf[atomInIdx & 63];
        LV2_Atom_Sequence* seq = (LV2_Atom_Sequence*)buf;
        // Build a sequence containing one note-on at frame 0.
        seq->atom.type = map_uri(NULL, LV2_ATOM__Sequence);
        seq->body.unit = 0; seq->body.pad = 0;
        LV2_Atom_Event* ev = (LV2_Atom_Event*)(buf + sizeof(LV2_Atom_Sequence));
        ev->time.frames = 0;
        ev->body.type = midiEventURID;
        ev->body.size = 3;
        uint8_t* msg = (uint8_t*)(ev + 1);
        msg[0] = 0x90; msg[1] = 60; msg[2] = 100; // note-on C4 vel 100
        seq->atom.size = sizeof(LV2_Atom_Sequence_Body)
                       + lv2_atom_pad_size(sizeof(LV2_Atom_Event) + 3);

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
            // sequence; DPF has already consumed the note, so leave it.
            seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
        }
        printf("\nMIDI->audio smoke (note-on C4 @ 2x): peak = %.4f (%s)\n",
               peak, peak > 1e-4f ? "AUDIBLE - PASS" : "SILENT - FAIL");
    }

    lilv_instance_deactivate(inst);
    lilv_instance_free(inst);
    printf("\nOK: instantiate + activate + run + latency read succeeded.\n");
    (void)defsNode;
    return 0;
}
