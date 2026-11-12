// panel_test.mm  (macOS only)
// Loads the built CLAP, opens its panel through the CLAP GUI extension in an
// offscreen window, renders screenshots, drives the controls with synthetic
// mouse events, checks that edits reach the host as gesture/value events,
// tests presets, and closes everything again.
//
//   panel_test <path to the .clap binary> <output dir for PNGs>

#import <Cocoa/Cocoa.h>

#include <clap/clap.h>

#include "Params.h"
#include "Editor.h"

#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

static int flushRequests = 0;
static void hpRescan (const clap_host_t*, clap_param_rescan_flags) {}
static void hpClear (const clap_host_t*, clap_id, clap_param_clear_flags) {}
static void hpRequestFlush (const clap_host_t*) { ++flushRequests; }
static const clap_host_params_t hostParams = { hpRescan, hpClear, hpRequestFlush };
static const void* hGetExt (const clap_host_t*, const char* id) { return ! std::strcmp (id, CLAP_EXT_PARAMS) ? &hostParams : nullptr; }
static void hNoop (const clap_host_t*) {}
static const clap_host_t host = { CLAP_VERSION_INIT, nullptr, "panel_test", "TB", "", "1.0", hGetExt, hNoop, hNoop, hNoop };

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
static uint32_t noEvents (const clap_input_events_t*) { return 0; }
static const clap_event_header_t* noGet (const clap_input_events_t*, uint32_t) { return nullptr; }

static NSData* renderPNG (NSView* v, const std::string& path)
{
    const NSRect b = [v bounds];
    NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect: b];
    [v cacheDisplayInRect: b toBitmapImageRep: rep];
    NSData* png = [rep representationUsingType: NSBitmapImageFileTypePNG properties: [NSDictionary dictionary]];
    [png writeToFile: [NSString stringWithUTF8String: path.c_str()] atomically: YES];
    std::printf ("  wrote %s (%lu bytes)\n", path.c_str(), (unsigned long) [png length]);
    return png;
}

static void mouse (NSWindow* w, NSView* v, NSEventType type, NSPoint panelPt, int clicks = 1)
{
    const NSPoint wp = [v convertPoint: panelPt toView: nil];
    NSEvent* e = [NSEvent mouseEventWithType: type location: wp modifierFlags: 0 timestamp: 0
                                windowNumber: [w windowNumber] context: nil eventNumber: 0
                                  clickCount: clicks pressure: 1.0f];
    if (type == NSEventTypeLeftMouseDown) [v mouseDown: e];
    else if (type == NSEventTypeLeftMouseDragged) [v mouseDragged: e];
    else [v mouseUp: e];
}
static void click (NSWindow* w, NSView* v, NSPoint pt, int clicks = 1)
{
    mouse (w, v, NSEventTypeLeftMouseDown, pt, clicks);
    mouse (w, v, NSEventTypeLeftMouseUp, pt, clicks);
}
static void pump (double secs) { [[NSRunLoop currentRunLoop] runUntilDate: [NSDate dateWithTimeIntervalSinceNow: secs]]; }

static double val (const clap_plugin_t* p, const clap_plugin_params_t* pp, int id)
{
    double v = -1; pp->get_value (p, (clap_id) id, &v); return v;
}

// One processing block, so queued panel edits reach the "host".
static void processBlock (const clap_plugin_t* p, OutEvents& out)
{
    std::vector<float> l (256, 0.0f), r (256, 0.0f), ol (256), orr (256);
    float* ins[2] = { &l[0], &r[0] };
    float* outs[2] = { &ol[0], &orr[0] };
    clap_audio_buffer_t ib; std::memset (&ib, 0, sizeof (ib)); ib.data32 = ins; ib.channel_count = 2;
    clap_audio_buffer_t ob; std::memset (&ob, 0, sizeof (ob)); ob.data32 = outs; ob.channel_count = 2;
    clap_input_events_t in = { nullptr, noEvents, noGet };
    clap_process_t pr; std::memset (&pr, 0, sizeof (pr));
    pr.steady_time = -1; pr.frames_count = 256;
    pr.audio_inputs = &ib; pr.audio_inputs_count = 1; pr.audio_outputs = &ob; pr.audio_outputs_count = 1;
    pr.in_events = &in; pr.out_events = &out.list;
    p->process (p, &pr);
}

// Play a sine through the plugin (so the LEDs follow a real signal)
static void playSine (const clap_plugin_t* p, double amp, int blocks)
{
    static double phase = 0.0;
    std::vector<float> l (256), r (256), ol (256), orr (256);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 256; ++i) { l[i] = r[i] = (float) (amp * std::sin (phase)); phase += 2.0 * 3.14159265358979 * 220.0 / 44100.0; }
        float* ins[2] = { &l[0], &r[0] };
        float* outs[2] = { &ol[0], &orr[0] };
        clap_audio_buffer_t ib; std::memset (&ib, 0, sizeof (ib)); ib.data32 = ins; ib.channel_count = 2;
        clap_audio_buffer_t ob; std::memset (&ob, 0, sizeof (ob)); ob.data32 = outs; ob.channel_count = 2;
        clap_input_events_t in = { nullptr, noEvents, noGet };
        OutEvents out;
        clap_process_t pr; std::memset (&pr, 0, sizeof (pr));
        pr.steady_time = -1; pr.frames_count = 256;
        pr.audio_inputs = &ib; pr.audio_inputs_count = 1; pr.audio_outputs = &ob; pr.audio_outputs_count = 1;
        pr.in_events = &in; pr.out_events = &out.list;
        p->process (p, &pr);
    }
}

static void setParam (const clap_plugin_t* p, const clap_plugin_params_t* pp, int id, double v)
{
    clap_event_param_value_t ev; std::memset (&ev, 0, sizeof (ev));
    ev.header.size = sizeof (ev); ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID; ev.header.type = CLAP_EVENT_PARAM_VALUE;
    ev.param_id = (clap_id) id; ev.value = v; ev.note_id = -1; ev.port_index = -1; ev.channel = -1; ev.key = -1;
    struct L { static uint32_t size (const clap_input_events_t*) { return 1; }
               static const clap_event_header_t* get (const clap_input_events_t* l, uint32_t) { return (const clap_event_header_t*) l->ctx; } };
    clap_input_events_t in = { &ev, L::size, L::get };
    OutEvents o; pp->flush (p, &in, &o.list);
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: %s <clap binary> <png dir>\n", argv[0]); return 2; }
    setvbuf (stdout, 0, _IONBF, 0);
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    const std::string dir = argv[2];

    const std::string presetFile = dir + "/test_presets/Presets.txt";
    setenv ("TBTYPEA_PRESET_FILE", presetFile.c_str(), 1);
    std::remove (presetFile.c_str());

    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::printf ("dlopen failed: %s\n", dlerror()); return 1; }
    const clap_plugin_entry_t* entry = (const clap_plugin_entry_t*) dlsym (lib, "clap_entry");
    entry->init (argv[1]);
    const clap_plugin_factory_t* f = (const clap_plugin_factory_t*) entry->get_factory (CLAP_PLUGIN_FACTORY_ID);
    const clap_plugin_t* p = f->create_plugin (f, &host, f->get_plugin_descriptor (f, 0)->id);
    p->init (p);
    p->activate (p, 44100.0, 1, 4096);
    p->start_processing (p);
    const clap_plugin_params_t* pp = (const clap_plugin_params_t*) p->get_extension (p, CLAP_EXT_PARAMS);

    std::printf ("Panel\n");
    const clap_plugin_gui_t* gui = (const clap_plugin_gui_t*) p->get_extension (p, CLAP_EXT_GUI);
    check (gui && gui->is_api_supported (p, CLAP_WINDOW_API_COCOA, false), "supports an embedded Cocoa panel");
    uint32_t w = 0, h = 0;
    check (gui->create (p, CLAP_WINDOW_API_COCOA, false) && gui->get_size (p, &w, &h) && w == (uint32_t) tbtypea::kEditorWidth && h == (uint32_t) tbtypea::kEditorHeight, "panel size as declared (1800 x 270)");

    NSWindow* win = [[NSWindow alloc] initWithContentRect: NSMakeRect (0, 0, w, h) styleMask: NSWindowStyleMaskBorderless
                                                  backing: NSBackingStoreBuffered defer: NO];
    NSView* parent = [win contentView];
    clap_window_t cw; cw.api = CLAP_WINDOW_API_COCOA; cw.cocoa = parent;
    check (gui->set_parent (p, &cw) && gui->show (p), "panel attaches to the host's view");
    check ([[parent subviews] count] == 1, "panel view added");
    NSView* panel = [[parent subviews] count] ? [[parent subviews] objectAtIndex: 0] : nil;
    if (! panel) return 1;
    NSData* pngDefault = renderPNG (panel, dir + "/panel_default.png");
    check ([pngDefault length] > 10000, "panel renders");

    std::printf ("\nControls\n");
    using namespace tbtypea;
    OutEvents out;
    setParam (p, pp, kB2Level, 0.5);
    const double g0 = val (p, pp, kB2Level);
    flushRequests = 0;
    mouse (win, panel, NSEventTypeLeftMouseDown,    NSMakePoint (760, 88));
    mouse (win, panel, NSEventTypeLeftMouseDragged, NSMakePoint (760, 63));
    mouse (win, panel, NSEventTypeLeftMouseDragged, NSMakePoint (760, 38));
    mouse (win, panel, NSEventTypeLeftMouseUp,      NSMakePoint (760, 38));
    const double g1 = val (p, pp, kB2Level);
    std::printf ("  BAND 2 level %.2f -> %.2f\n", g0, g1);
    check (std::fabs (g1 - std::min (1.0, g0 + 0.25)) < 0.02, "dragging a knob up turns it up");
    check (flushRequests > 0, "the host is asked to collect the edit");
    processBlock (p, out);
    check (out.begins == 1 && out.ends == 1 && out.values >= 1, "edit reaches the host as begin / value / end");

    click (win, panel, NSMakePoint (760, 88), 2);
    check (std::fabs (val (p, pp, kB2Level) - kDefaults[kB2Level]) < 1e-6, "double-click resets a knob");
    {
        const int knobs[7] = { kStress, kB1Level, kB2Level, kB3Level, kB4Level, kInput, kOutput };
        const CGFloat kx[7] = { 500, 610, 760, 910, 1060, 1270, 1270 };
        const CGFloat ky[7] = { 88, 88, 88, 88, 88, 54, 116 };
        bool all = true;
        for (int k = 0; k < 7; ++k)
        {
            setParam (p, pp, knobs[k], 0.5);
            const double v0 = val (p, pp, knobs[k]);
            mouse (win, panel, NSEventTypeLeftMouseDown,    NSMakePoint (kx[k], ky[k]));
            mouse (win, panel, NSEventTypeLeftMouseDragged, NSMakePoint (kx[k], ky[k] + 20));
            mouse (win, panel, NSEventTypeLeftMouseUp,      NSMakePoint (kx[k], ky[k] + 20));
            if (! (val (p, pp, knobs[k]) < v0 - 0.05)) { all = false; std::printf ("  knob %d did not turn\n", knobs[k]); }
            setParam (p, pp, knobs[k], kDefaults[knobs[k]]);
        }
        check (all, "all seven knobs and trimpots turn");
    }

    // Lamp buttons: BAND 1-4, NR, Dolby tone
    {
        const int sw[4] = { kB1On, kB2On, kB3On, kB4On };
        const CGFloat sx[4] = { 680, 830, 980, 1130 };
        bool all = true;
        for (int k = 0; k < 4; ++k) { click (win, panel, NSMakePoint (sx[k], 88)); if (val (p, pp, sw[k]) != 0.0) all = false; }
        check (all, "BAND 1-4 buttons switch their bands off");
        click (win, panel, NSMakePoint (980, 88));
        click (win, panel, NSMakePoint (1130, 88));
        check (val (p, pp, kB3On) == 1.0 && val (p, pp, kB4On) == 1.0, "and back on");
        pump (0.1);
        renderPNG (panel, dir + "/panel_bands_3_4.png");
        click (win, panel, NSMakePoint (680, 88));
        click (win, panel, NSMakePoint (830, 88));
    }
    click (win, panel, NSMakePoint (345, 106));
    check (val (p, pp, kNr) == 0.0, "NR button switches NR out");
    pump (0.1);
    renderPNG (panel, dir + "/panel_nr_out.png");
    click (win, panel, NSMakePoint (345, 106));
    check (val (p, pp, kNr) == 1.0, "and NR back in");
    {
        double before[kNumParams];
        for (int i = 0; i < kNumParams; ++i) before[i] = val (p, pp, i);
        click (win, panel, NSMakePoint (415, 106));
        bool same = true;
        for (int i = 0; i < kNumParams; ++i) if (val (p, pp, i) != before[i]) same = false;
        check (same, "the rec lamp is always lit and changes nothing");
    }
    click (win, panel, NSMakePoint (190, 45));
    check (val (p, pp, kTone) == 1.0, "Dolby tone button switches the tone on");
    playSine (p, 0.0, 40);
    pump (0.3);
    renderPNG (panel, dir + "/panel_tone.png");
    click (win, panel, NSMakePoint (190, 45));
    check (val (p, pp, kTone) == 0.0, "and off");

    std::printf ("\nPresets\n");
    #define SLOT(k) NSMakePoint (84.0 + (k) * 33.0 + 14.5, 214.0)
    setParam (p, pp, kB4Level, 0.3);
    setParam (p, pp, kB1On, 0.0);
    pump (0.1);
    click (win, panel, NSMakePoint (53, 214));     // SAVE
    click (win, panel, SLOT (2));                  // slot 3
    FILE* pf = std::fopen (presetFile.c_str(), "r");
    check (pf != nullptr, "SAVE then a slot writes the preset file");
    if (pf) std::fclose (pf);
    renderPNG (panel, dir + "/panel_preset_saved.png");

    setParam (p, pp, kB4Level, 0.9);
    setParam (p, pp, kB1On, 1.0);
    OutEvents rec;
    click (win, panel, SLOT (2));
    processBlock (p, rec);
    check (std::fabs (val (p, pp, kB4Level) - 0.3) < 1e-5 && val (p, pp, kB1On) == 0.0,
           "clicking the slot recalls the saved settings");
    check (rec.values >= kNumParams, "recall tells the host about every parameter");
    const double before = val (p, pp, kB4Level);
    click (win, panel, SLOT (5));
    check (val (p, pp, kB4Level) == before, "an empty slot changes nothing");

    std::printf ("\nHost changes show on the panel\n");
    for (int i = 0; i < kNumParams; ++i) setParam (p, pp, i, kDefaults[i]);
    pump (0.2);
    NSData* pngBefore = renderPNG (panel, dir + "/panel_before_host.png");
    setParam (p, pp, kStress, 0.6);
    setParam (p, pp, kNr, 0.0);
    pump (0.3);
    NSData* pngAfter = renderPNG (panel, dir + "/panel_changed.png");
    check (! [pngAfter isEqualToData: pngBefore], "panel redraws after host changes");

    std::printf ("\nMeter follows the circuit\n");
    {
        for (int i = 0; i < kNumParams; ++i) setParam (p, pp, i, kDefaults[i]);
        playSine (p, 0.0, 40);
        pump (0.3);
        NSData* quiet = renderPNG (panel, dir + "/panel_meter_rest.png");
        playSine (p, 0.17803, 86);      // Dolby level
        pump (0.3);
        NSData* lit = renderPNG (panel, dir + "/panel_meter_lit.png");
        check (! [lit isEqualToData: quiet], "meter needle moves while playing");
    }

    std::printf ("\nClose\n");
    gui->hide (p);
    gui->destroy (p);
    check ([[parent subviews] count] == 0, "destroying the GUI removes the panel");
    check (gui->create (p, CLAP_WINDOW_API_COCOA, false) && gui->set_parent (p, &cw) && [[parent subviews] count] == 1, "reopens");
    {
        NSView* v2 = [[parent subviews] objectAtIndex: 0];
        setParam (p, pp, kB1On, 0.0);        // the vocal stresser: bands 3 and 4, STRESS up
        setParam (p, pp, kB2On, 0.0);
        setParam (p, pp, kStress, 0.75);
        click (win, v2, NSMakePoint (500, 88));          // readout shows STRESS
        playSine (p, 0.17803, 86);
        pump (0.3);
        renderPNG (v2, dir + "/panel_showcase.png");
    }
    gui->destroy (p);
    p->stop_processing (p);
    p->deactivate (p);
    p->destroy (p);
    entry->deinit();
    [win release];
    [pool release];
    dlclose (lib);

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PANEL CHECKS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
