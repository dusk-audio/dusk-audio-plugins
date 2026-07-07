// clap_ab_host.cpp — minimal headless CLAP host for A/B validation.
// Loads any .clap, sets params by DISPLAY TEXT (via the plugin's text_to_value),
// renders a deterministic signal (white noise / impulse), writes a float32 WAV.
// Used to A/B the framework-free DSP cores against the JUCE build's DSP (which
// clap-juce-extensions wraps unchanged). Feed identical input to core + CLAP,
// cross-correlate to remove the plugin's reported latency, then diff samples.
//   g++ -std=c++17 -O2 -I<clap>/include clap_ab_host.cpp -o clap_ab_host -ldl

// Minimal headless CLAP host — loads a .clap, sets params by name via input
// events, renders a fixed input signal in stereo, writes a float32 WAV.
// Purpose: A/B the Multi-Comp multiband mix for comb filtering under CLAP.
#include <clap/clap.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <random>

static const void* host_get_ext(const clap_host_t*, const char*) { return nullptr; }
static void host_noop(const clap_host_t*) {}
static void host_req(const clap_host_t*) {}

// ---- event lists -----------------------------------------------------------
struct InEvents {
    clap_input_events_t api;
    std::vector<clap_event_param_value_t> evs;
};
static uint32_t in_size(const clap_input_events_t* l) {
    return (uint32_t)((InEvents*)l->ctx)->evs.size();
}
static const clap_event_header_t* in_get(const clap_input_events_t* l, uint32_t i) {
    return &((InEvents*)l->ctx)->evs[i].header;
}
static bool out_push(const clap_output_events_t*, const clap_event_header_t*) { return true; }

// ---- WAV (float32) ---------------------------------------------------------
static void writeWavF32(const char* path, const std::vector<float>& interleaved,
                        int ch, int sr) {
    FILE* f = fopen(path, "wb");
    uint32_t dataBytes = (uint32_t)(interleaved.size() * 4);
    uint32_t byteRate = (uint32_t)(sr * ch * 4);
    uint16_t blockAlign = (uint16_t)(ch * 4);
    fwrite("RIFF", 1, 4, f);
    uint32_t riff = 36 + dataBytes; fwrite(&riff, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtLen = 16; fwrite(&fmtLen, 4, 1, f);
    uint16_t fmt = 3; fwrite(&fmt, 2, 1, f);            // IEEE float
    uint16_t chs = (uint16_t)ch; fwrite(&chs, 2, 1, f);
    uint32_t srate = (uint32_t)sr; fwrite(&srate, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    uint16_t bits = 32; fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataBytes, 4, 1, f);
    fwrite(interleaved.data(), 4, interleaved.size(), f);
    fclose(f);
}

int main(int argc, char** argv) {
    // args: --clap PATH --out WAV --mix N [--signal noise|impulse] [--param "Name=val"]...
    std::string clapPath, outPath, signal = "noise";
    // params set by DISPLAY TEXT (converted via the plugin's text_to_value, so
    // callers pass human values like "Band 2 Gain=6" regardless of normalisation).
    std::vector<std::pair<std::string,std::string>> textParams;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--clap" && i+1 < argc) clapPath = argv[++i];
        else if (a == "--out" && i+1 < argc) outPath = argv[++i];
        else if (a == "--signal" && i+1 < argc) signal = argv[++i];
        else if (a == "--param" && i+1 < argc) {
            std::string kv = argv[++i]; auto eq = kv.find('=');
            textParams.push_back({kv.substr(0,eq), kv.substr(eq+1)});
        }
    }
    const int sr = 48000, ch = 2, block = 512;
    const int totalFrames = sr * 2; // 2 s

    void* lib = dlopen(clapPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "dlopen fail: %s\n", dlerror()); return 1; }
    auto* entry = (const clap_plugin_entry_t*)dlsym(lib, "clap_entry");
    if (!entry) { fprintf(stderr, "no clap_entry\n"); return 1; }
    entry->init(clapPath.c_str());
    auto* factory = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    uint32_t n = factory->get_plugin_count(factory);
    if (n == 0) { fprintf(stderr, "no plugins\n"); return 1; }
    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);

    clap_host_t host{};
    host.clap_version = CLAP_VERSION;
    host.host_data = nullptr;
    host.name = "mc-claphost"; host.vendor = "dusk"; host.url = ""; host.version = "1";
    host.get_extension = host_get_ext;
    host.request_restart = host_req; host.request_process = host_req;
    host.request_callback = host_req;

    const clap_plugin_t* plug = factory->create_plugin(factory, &host, desc->id);
    if (!plug) { fprintf(stderr, "create_plugin fail\n"); return 1; }
    if (!plug->init(plug)) { fprintf(stderr, "init fail\n"); return 1; }

    // params ext: match names → ids
    auto* params = (const clap_plugin_params_t*)plug->get_extension(plug, CLAP_EXT_PARAMS);
    if (!params) { fprintf(stderr, "no params ext\n"); return 1; }
    uint32_t pc = params->count(plug);
    auto findId = [&](const std::string& name, clap_id& out, double& mn, double& mx) -> bool {
        for (uint32_t i = 0; i < pc; ++i) {
            clap_param_info_t info{};
            if (params->get_info(plug, i, &info) && name == info.name) {
                out = info.id; mn = info.min_value; mx = info.max_value; return true;
            }
        }
        return false;
    };

    InEvents in{};
    in.api.ctx = &in;
    in.api.size = in_size; in.api.get = in_get;
    auto pushParamText = [&](const std::string& name, const std::string& text) {
        clap_id id; double mn, mx;
        if (!findId(name, id, mn, mx)) { fprintf(stderr, "  ! param not found: %s\n", name.c_str()); return; }
        double val = 0.0;
        if (!params->text_to_value(plug, id, text.c_str(), &val)) {
            fprintf(stderr, "  ! text_to_value failed: %s='%s'\n", name.c_str(), text.c_str());
            return;
        }
        clap_event_param_value_t e{};
        e.header.size = sizeof(e);
        e.header.time = 0;
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = CLAP_EVENT_PARAM_VALUE;
        e.header.flags = 0;
        e.param_id = id; e.cookie = nullptr;
        e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
        e.value = val;
        in.evs.push_back(e);
        fprintf(stderr, "  param %-22s -> '%s' (norm %.4f)\n", name.c_str(), text.c_str(), val);
    };
    for (auto& kv : textParams) pushParamText(kv.first, kv.second);

    clap_output_events_t out{}; out.ctx = nullptr; out.try_push = out_push;

    plug->activate(plug, sr, block, block);
    plug->start_processing(plug);

    // input signal: deterministic white noise (fixed seed) or impulse
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-0.25f, 0.25f);

    std::vector<float> L(block), R(block);
    float* chans[2] = { L.data(), R.data() };
    clap_audio_buffer_t abIn{}, abOut{};
    abIn.data32 = chans; abIn.channel_count = ch; abIn.latency = 0; abIn.constant_mask = 0;
    abOut.data32 = chans; abOut.channel_count = ch; abOut.latency = 0; abOut.constant_mask = 0;

    std::vector<float> outInterleaved; outInterleaved.reserve((size_t)totalFrames * ch);

    int64_t steady = 0;
    for (int pos = 0; pos < totalFrames; pos += block) {
        int fr = std::min(block, totalFrames - pos);
        for (int i = 0; i < fr; ++i) {
            float s;
            if (signal == "impulse") s = (pos + i == 0) ? 1.0f : 0.0f;
            else s = dist(rng);
            L[i] = s; R[i] = s;
        }
        clap_process_t p{};
        p.steady_time = steady;
        p.frames_count = (uint32_t)fr;
        p.transport = nullptr;
        p.audio_inputs = &abIn; p.audio_outputs = &abOut;
        p.audio_inputs_count = 1; p.audio_outputs_count = 1;
        p.in_events = &in.api; p.out_events = &out;
        plug->process(plug, &p);
        // events consumed after first block
        if (pos == 0) in.evs.clear();
        for (int i = 0; i < fr; ++i) { outInterleaved.push_back(L[i]); outInterleaved.push_back(R[i]); }
        steady += fr;
    }

    plug->stop_processing(plug);
    plug->deactivate(plug);
    plug->destroy(plug);
    entry->deinit();
    dlclose(lib);

    writeWavF32(outPath.c_str(), outInterleaved, ch, sr);
    fprintf(stderr, "wrote %s (%d frames)\n", outPath.c_str(), totalFrames);
    return 0;
}
