// clap_host_test.cpp
// A minimal CLAP host: loads the built plugin binary, then checks the
// descriptor, parameters, text conversion, processing, automation events,
// state save/restore, latency and tail.
//
//   clap_host_test <path to the .clap binary>
// On macOS pass the binary inside the bundle:
//   "TB Type A 360.clap/Contents/MacOS/TB Type A 360"

#include <clap/clap.h>

#include "Params.h"
#include "TypeA360.h"

#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <string>
#include <vector>

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

// ---- Host ----
static int flushRequests = 0, rescans = 0;
static void hpRescan (const clap_host_t*, clap_param_rescan_flags) { ++rescans; }
static void hpClear (const clap_host_t*, clap_id, clap_param_clear_flags) {}
static void hpRequestFlush (const clap_host_t*) { ++flushRequests; }
static const clap_host_params_t hostParams = { hpRescan, hpClear, hpRequestFlush };

static const void* hGetExt (const clap_host_t*, const char* id)
{
    return ! std::strcmp (id, CLAP_EXT_PARAMS) ? &hostParams : nullptr;
}
static void hNoop (const clap_host_t*) {}
static const clap_host_t host = { CLAP_VERSION_INIT, nullptr, "clap_host_test", "TB", "", "1.0",
                                  hGetExt, hNoop, hNoop, hNoop };

// ---- Event lists ----
struct InEvents
{
    std::vector<clap_event_param_value_t> ev;
    clap_input_events_t list;
    InEvents() { list.ctx = this; list.size = &sSize; list.get = &sGet; }
    static uint32_t sSize (const clap_input_events_t* l) { return (uint32_t) ((InEvents*) l->ctx)->ev.size(); }
    static const clap_event_header_t* sGet (const clap_input_events_t* l, uint32_t i) { return &((InEvents*) l->ctx)->ev[i].header; }
    void add (clap_id id, double v)
    {
        clap_event_param_value_t e;
        std::memset (&e, 0, sizeof (e));
        e.header.size = sizeof (e); e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_PARAM_VALUE;
        e.param_id = id; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v;
        ev.push_back (e);
    }
};

struct OutEvents
{
    int values = 0, begins = 0, ends = 0;
    clap_output_events_t list;
    OutEvents() { list.ctx = this; list.try_push = &sPush; }
    static bool sPush (const clap_output_events_t* l, const clap_event_header_t* h)
    {
        OutEvents* o = (OutEvents*) l->ctx;
        if (h->type == CLAP_EVENT_PARAM_VALUE) ++o->values;
        if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN) ++o->begins;
        if (h->type == CLAP_EVENT_PARAM_GESTURE_END) ++o->ends;
        return true;
    }
};

// ---- Streams ----
struct OStream { std::string data; clap_ostream_t s; OStream() { s.ctx = this; s.write = &w; }
    static int64_t w (const clap_ostream_t* st, const void* b, uint64_t n) { ((OStream*) st->ctx)->data.append ((const char*) b, (size_t) n); return (int64_t) n; } };
struct IStream { std::string data; size_t pos = 0; clap_istream_t s; IStream() { s.ctx = this; s.read = &r; }
    static int64_t r (const clap_istream_t* st, void* b, uint64_t n)
    {
        IStream* is = (IStream*) st->ctx;
        // Hand the data over a few bytes at a time to prove partial reads work
        size_t k = std::min ((size_t) n, std::min ((size_t) 37, is->data.size() - is->pos));
        std::memcpy (b, is->data.data() + is->pos, k); is->pos += k; return (int64_t) k;
    } };

static bool finite (const std::vector<float>& x, float lim)
{
    for (size_t i = 0; i < x.size(); ++i) if (! (x[i] == x[i]) || std::fabs (x[i]) > lim) return false;
    return true;
}

// Run audio through the plugin in blocks.
static void run (const clap_plugin_t* p, std::vector<float>& l, std::vector<float>& r,
                 std::vector<float>& ol, std::vector<float>& orr, uint32_t block,
                 InEvents* in = nullptr, OutEvents* out = nullptr)
{
    InEvents emptyIn; OutEvents sink;
    for (size_t pos = 0; pos < l.size(); pos += block)
    {
        const uint32_t n = (uint32_t) std::min ((size_t) block, l.size() - pos);
        float* ins[2] = { &l[pos], &r[pos] };
        float* outs[2] = { &ol[pos], &orr[pos] };
        clap_audio_buffer_t ib; std::memset (&ib, 0, sizeof (ib)); ib.data32 = ins; ib.channel_count = 2;
        clap_audio_buffer_t ob; std::memset (&ob, 0, sizeof (ob)); ob.data32 = outs; ob.channel_count = 2;
        clap_process_t pr; std::memset (&pr, 0, sizeof (pr));
        pr.steady_time = -1; pr.frames_count = n;
        pr.audio_inputs = &ib; pr.audio_inputs_count = 1;
        pr.audio_outputs = &ob; pr.audio_outputs_count = 1;
        pr.in_events = (in && pos == 0) ? &in->list : &emptyIn.list;
        pr.out_events = out ? &out->list : &sink.list;
        p->process (p, &pr);
    }
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: %s <plugin binary>\n", argv[0]); return 2; }
    std::printf ("Loading %s\n", argv[1]);
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::printf ("dlopen failed: %s\n", dlerror()); return 1; }
    const clap_plugin_entry_t* entry = (const clap_plugin_entry_t*) dlsym (lib, "clap_entry");
    check (entry != nullptr, "exports clap_entry");
    if (! entry) return 1;
    check (entry->init (argv[1]), "entry init");
    const clap_plugin_factory_t* f = (const clap_plugin_factory_t*) entry->get_factory (CLAP_PLUGIN_FACTORY_ID);
    check (f && f->get_plugin_count (f) == 1, "one plugin in the factory");
    const clap_plugin_descriptor_t* d = f->get_plugin_descriptor (f, 0);
    std::printf ("  %s by %s, id %s, version %s\n", d->name, d->vendor, d->id, d->version);
    check (! std::strcmp (d->name, "TB Type A 360"), "plugin name");

    const clap_plugin_t* p = f->create_plugin (f, &host, d->id);
    check (p && p->init (p), "create and init");
    if (! p) return 1;

    using namespace tbtypea;
    std::printf ("\nParameters\n");
    const clap_plugin_params_t* pp = (const clap_plugin_params_t*) p->get_extension (p, CLAP_EXT_PARAMS);
    check (pp && pp->count (p) == kNumParams && kNumParams == 13, "13 parameters");
    const char* order[kNumParams] = { "Dolby Tone", "NR In/Out", "Stress",
                                      "Band 1 On", "Band 1 Level", "Band 2 On", "Band 2 Level", "Band 3 On", "Band 3 Level",
                                      "Band 4 On", "Band 4 Level", "Input Trim", "Output Trim" };
    bool namesOk = true, defaultsOk = true;
    for (uint32_t i = 0; i < pp->count (p); ++i)
    {
        clap_param_info_t info;
        pp->get_info (p, i, &info);
        double v = -1; pp->get_value (p, info.id, &v);
        char txt[64] = { 0 }; pp->value_to_text (p, info.id, v, txt, sizeof (txt));
        std::printf ("  %-10s %-10s = %-9s (%.2f, %g..%g%s)\n", info.name, info.module, txt, v, info.min_value, info.max_value,
                     (info.flags & CLAP_PARAM_IS_STEPPED) ? ", stepped" : "");
        if (std::strcmp (info.name, order[i]) != 0) namesOk = false;
        if (std::fabs (v - info.default_value) > 1e-6) defaultsOk = false;
    }
    check (namesOk, "parameters in front-panel order");
    check (defaultsOk, "values start at their defaults");
    double tv = 0;
    check (pp->text_to_value (p, kStress, "+18", &tv) && std::fabs (tv - 0.75) < 1e-9, "text '+18' -> STRESS +18 dB");
    check (pp->text_to_value (p, kB3Level, "-12", &tv) && std::fabs (TypeA360::bandLevelDb (tv) + 12.0) < 0.01, "text '-12' -> band level -12 dB");
    check (pp->text_to_value (p, kB3Level, "Off", &tv) && tv == 0.0, "text 'Off' -> band level off");
    check (pp->text_to_value (p, kInput, "-25", &tv) && std::fabs (inputDb (tv) + 25.0) < 1e-9, "text '-25' -> input trim -25 dB");
    check (pp->text_to_value (p, kNr, "Out", &tv) && tv == 0.0, "text 'Out' -> NR out");
    check (pp->text_to_value (p, kB1On, "Off", &tv) && tv == 0.0, "text 'Off' -> band off");
    char txt[32];
    pp->value_to_text (p, kB4Level, 1.0, txt, sizeof (txt));
    check (! std::strcmp (txt, "+18.0 dB"), "band level reads +18.0 dB at 10");
    pp->value_to_text (p, kB4Level, 0.5, txt, sizeof (txt));
    check (! std::strcmp (txt, "0.0 dB"), "band level reads 0.0 dB at stock");
    pp->value_to_text (p, kStress, 1.0, txt, sizeof (txt));
    check (! std::strcmp (txt, "+24.0 dB"), "STRESS reads +24.0 dB at max");
    pp->value_to_text (p, kInput, 0.0, txt, sizeof (txt));
    check (! std::strcmp (txt, "-40.0 dB"), "input trim reads -40 dB at minimum");
    pp->value_to_text (p, kNr, 1.0, txt, sizeof (txt));
    check (! std::strcmp (txt, "In"), "NR reads In");

    std::printf ("\nAudio\n");
    const clap_plugin_audio_ports_t* ap = (const clap_plugin_audio_ports_t*) p->get_extension (p, CLAP_EXT_AUDIO_PORTS);
    clap_audio_port_info_t pi;
    check (ap && ap->count (p, true) == 1 && ap->count (p, false) == 1 && ap->get (p, 0, false, &pi) && pi.channel_count == 2,
           "one stereo input and output");
    const clap_plugin_latency_t* lat = (const clap_plugin_latency_t*) p->get_extension (p, CLAP_EXT_LATENCY);
    const clap_plugin_tail_t* tail = (const clap_plugin_tail_t*) p->get_extension (p, CLAP_EXT_TAIL);
    check (p->activate (p, 44100.0, 1, 4096), "activate at 44.1 kHz");
    p->start_processing (p);
    check (lat && lat->get (p) == (uint32_t) TypeA360::latencySamples(), "reports the oversampler latency");
    check (tail && tail->get (p) == 0, "no tail");

    const int sr = 44100;
    std::vector<float> l (sr), r (sr), ol (sr), orr (sr);
    for (int i = 0; i < sr; ++i) l[i] = r[i] = (float) (0.2 * std::sin (2.0 * 3.14159265358979 * 196.0 * i / sr));

    // Reference: the DSP core directly, same settings as the defaults
    {
        TypeA360* ref = new TypeA360();
        ref->setDolbyTone (false); ref->setNrIn (true); ref->setStressDb (0.0);
        for (int b = 0; b < kNumBands; ++b) { ref->setBandOn (b, true); ref->setBandLevel (b, 0.5); }
        ref->setInputGainDb (inputDb (kDefaults[kInput])); ref->setOutputGainDb (outputDb (kDefaults[kOutput]));
        ref->prepare (sr);
        std::vector<float> refOut (sr);
        ref->process (&l[0], &refOut[0], sr);
        delete ref;

        const std::clock_t t0 = std::clock();
        run (p, l, r, ol, orr, 512);
        const double secs = (double) (std::clock() - t0) / CLOCKS_PER_SEC;
        float pk = 0, lr = 0; double diff = 0;
        for (int i = 0; i < sr; ++i)
        {
            pk = std::max (pk, std::fabs (ol[i]));
            lr = std::max (lr, std::fabs (ol[i] - orr[i]));
            diff = std::max (diff, (double) std::fabs (ol[i] - refOut[i]));
        }
        std::printf ("  peak %.3f, max diff vs DSP core %.2g, CPU %.1f%% of one core\n", pk, diff, 100.0 * secs);
        check (pk > 0.02 && finite (ol, 10.0f), "produces output");
        check (diff < 1e-6, "matches the DSP core sample for sample (same as the 10.8.5 build)");
        check (lr == 0.0f, "a mono source gives identical sides");
    }

    // Stereo source: sides kept apart, circuits in step
    {
        std::vector<float> a (sr), b (sr, 0.0f), oa (sr), ob (sr);
        for (int i = 0; i < sr; ++i) a[i] = (float) (0.2 * std::sin (2.0 * 3.14159265358979 * 330.0 * i / sr));
        run (p, a, b, oa, ob, 256);
        float pl = 0, pr = 0;
        for (int i = sr / 2; i < sr; ++i) { pl = std::max (pl, std::fabs (oa[i])); pr = std::max (pr, std::fabs (ob[i])); }
        std::printf ("  left-only input: left %.3f, right %.4f\n", pl, pr);
        check (pl > 0.02f && pr < 0.01f * pl, "stereo sides processed separately");
        run (p, a, a, oa, ob, 256);
        float dd = 0;
        for (int i = sr / 2; i < sr; ++i) dd = std::max (dd, std::fabs (oa[i] - ob[i]));
        check (dd < 1e-6f, "identical sides stay identical");
    }

    run (p, l, r, l, r, 64);
    check (finite (l, 10.0f) && finite (r, 10.0f), "in-place buffers");
    for (int i = 0; i < sr; ++i) l[i] = r[i] = (float) (0.2 * std::sin (2.0 * 3.14159265358979 * 196.0 * i / sr));

    std::printf ("\nNR in / out\n");
    {
        // NR out: the unit as a fixed-gain amplifier (unity at Dolby level)
        InEvents in; in.add (kNr, 0.0);
        std::vector<float> a (sr), b (sr), oa (sr), ob (sr);
        for (int i = 0; i < sr; ++i) a[i] = b[i] = (float) (0.17803 * std::sin (2.0 * 3.14159265358979 * 1000.0 * i / sr));
        run (p, a, b, oa, ob, 512, &in);
        double s0 = 0, s1 = 0;
        for (int i = sr / 2; i < sr; ++i) { s0 += (double) oa[i] * oa[i]; s1 += (double) a[i] * a[i]; }
        const double g = 10.0 * std::log10 (s0 / s1);
        std::printf ("  NR out gain at Dolby level %+.2f dB\n", g);
        check (std::fabs (g) < 0.3, "NR out: unity gain");
        InEvents back; back.add (kNr, 1.0);
        std::vector<float> s1v (512, 0.0f), s2 (512, 0.0f), o1 (512), o2 (512);
        run (p, s1v, s2, o1, o2, 512, &back);
    }

    std::printf ("\nAutomation\n");
    {
        InEvents in; in.add (kStress, 0.9); in.add (kB2Level, 0.2); in.add (kNr, 0.0);
        std::vector<float> a (4096, 0.1f), b (4096, 0.1f), oa (4096), ob (4096);
        run (p, a, b, oa, ob, 512, &in);
        double v;
        pp->get_value (p, kStress, &v);  check (std::fabs (v - 0.9) < 1e-6, "host automation sets STRESS");
        pp->get_value (p, kNr, &v);  check (v == 0.0, "host automation switches NR out");
        check (finite (oa, 10.0f), "output stays finite while automated");
        // Every control swept while playing
        std::vector<float> c (sr), cr (sr), oc (sr), ocr (sr);
        for (int i = 0; i < sr; ++i) c[i] = cr[i] = (float) (0.3 * std::sin (2.0 * 3.14159265358979 * 110.0 * i / sr));
        bool ok = true;
        for (size_t pos = 0; pos < c.size(); pos += 64)
        {
            InEvents e;
            const double t = (double) pos / c.size();
            for (int k = 0; k < kNumParams; ++k) e.add ((clap_id) k, 0.5 + 0.5 * std::sin (6.2831853 * (k + 1) * t));
            std::vector<float> a1 (c.begin() + pos, c.begin() + std::min (c.size(), pos + 64));
            std::vector<float> a2 (a1), o1 (a1.size()), o2 (a1.size());
            run (p, a1, a2, o1, o2, 64, &e);
            if (! finite (o1, 100.0f)) ok = false;
        }
        check (ok, "all parameters automated together");
    }

    std::printf ("\nState\n");
    {
        const clap_plugin_state_t* st = (const clap_plugin_state_t*) p->get_extension (p, CLAP_EXT_STATE);
        InEvents set; set.add (kStress, 0.9); set.add (kB2Level, 0.2); set.add (kNr, 0.0);
        std::vector<float> a0 (512, 0.0f), b0 (512, 0.0f), oa0 (512), ob0 (512);
        run (p, a0, b0, oa0, ob0, 512, &set);
        OStream os;
        check (st && st->save (p, &os.s) && os.data.size() > 20, "saves state");
        InEvents in; in.add (kStress, 0.1); in.add (kNr, 1.0); in.add (kB2Level, 0.8);
        std::vector<float> a (512, 0.0f), b (512, 0.0f), oa (512), ob (512);
        run (p, a, b, oa, ob, 512, &in);
        IStream is; is.data = os.data;
        rescans = 0;
        check (st->load (p, &is.s), "loads state");
        double g, t, su;
        pp->get_value (p, kStress, &g); pp->get_value (p, kNr, &t); pp->get_value (p, kB2Level, &su);
        check (std::fabs (g - 0.9) < 1e-6 && t == 0.0 && std::fabs (su - 0.2) < 1e-6, "state restores every value");
        check (rescans > 0, "host is told to re-read the values");
        IStream junk; junk.data = "not a state";
        check (! st->load (p, &junk.s), "rejects foreign data");
        InEvents on;
        for (int k = 0; k < kNumParams; ++k) on.add ((clap_id) k, kDefaults[k]);
        run (p, a, b, oa, ob, 512, &on);
    }

    std::printf ("\nFlush while stopped\n");
    {
        p->stop_processing (p);
        InEvents in; in.add (kStress, 0.25);
        OutEvents out;
        pp->flush (p, &in.list, &out.list);
        double v; pp->get_value (p, kStress, &v);
        check (std::fabs (v - 0.25) < 1e-6, "flush applies host changes without audio running");
    }

    std::printf ("\nSample-rate change to 96 kHz\n");
    p->deactivate (p);
    check (p->activate (p, 96000.0, 1, 4096), "re-activate at 96 kHz");
    p->start_processing (p);
    {
        std::vector<float> a (96000), b (96000), oa (96000), ob (96000);
        for (int i = 0; i < 96000; ++i) { a[i] = (float) (0.2 * std::sin (2.0 * 3.14159265358979 * 196.0 * i / 96000)); b[i] = 0.5f * a[i]; }
        run (p, a, b, oa, ob, 1024);
        check (finite (oa, 10.0f) && finite (ob, 10.0f), "runs at 96 kHz");
    }
    // Silence settles to silence (STRESS full, input trim up)
    {
        InEvents in; in.add (kStress, 1.0); in.add (kInput, 1.0);
        std::vector<float> z (96000 * 4, 0.0f), z2 (96000 * 4, 0.0f), oa (96000 * 4), ob (96000 * 4);
        run (p, z, z2, oa, ob, 1024, &in);
        float t = 0; for (size_t i = 96000 * 3; i < oa.size(); ++i) t = std::max (t, std::fabs (oa[i]));
        std::printf ("  residual after 3 s of silence: %.2g\n", t);
        check (t < 1e-4f, "silence settles to silence");
    }

    p->stop_processing (p);
    p->deactivate (p);
    p->destroy (p);
    entry->deinit();
    check (true, "destroy and deinit");
    dlclose (lib);

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CLAP HOST CHECKS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
