// PanelView.mm
// The custom panel for TB Type A 360: an original design laid out like the
// user's Model 360 (photo): a 1U 19" rack face in the unit's proportions with
// silver ears, the silver control plate on the left (Dolby tone button over
// the small level meter, then the NR (in-out) and rec lamp pushbuttons; rec is
// always lit: this unit only records) widened to the right to take the band
// mod (BAND 1-4: a level pot and a lit on/off pushbutton each, the band's
// frequencies underneath), and a narrower black module cover on the right
// with the input and output trimpots by the module, the user's TB electronic
// logo, a plain red stripe and the red box. A strip along the bottom holds
// the presets and the readout.
//
// Every pushbutton lights up when engaged (the user's standard).
// Cocoa, manual retain/release, and only APIs that exist in Mac OS X 10.8.5.
// Everything is drawn in code; no image files.

#import <Cocoa/Cocoa.h>

#include "Editor.h"
#include "Params.h"
#include "Presets.h"
#include "TBLogo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tbtypea;

namespace
{

enum CtlType { kCtlKnob, kCtlTrim, kCtlLamp, kCtlTone };
enum LampColour { kLampNone, kLampWhite, kLampRed, kLampGreen, kLampAmber };

struct Ctl
{
    int param;
    int type;
    CGFloat cx, cy, w, h;  // knobs: w = radius; buttons: size
    int lamp;
    const char* label;     // readout name
};

// The rack face (1800 wide: 19" x 1U is 165.8 high), then the strip
const CGFloat kFaceH = 166.0;
const CGFloat kStripTop = 166.0;
const NSRect kSilverBox = { { 70.0, 12.0 }, { 1120.0, 142.0 } };
const NSRect kBlackBox  = { { 1206.0, 10.0 }, { 524.0, 146.0 } };

// Meter: face, and the needle's pivot (below the face, hidden)
const NSRect kMeterFace = { { 120.0, 63.0 }, { 140.0, 64.0 } };
const NSPoint kMeterPivot = { 190.0, 158.0 };

// Bands: column centres
inline CGFloat bandX (int b) { return 640.0 + 150.0 * b; }
const CGFloat kBandKnobY = 88.0;

const Ctl kCtls[] = {
    { kTone,    kCtlTone,  190, 45,  40, 20, kLampAmber, "DOLBY TONE" },
    { kNr,      kCtlLamp,  345, 106, 54, 46, kLampWhite, "NR" },
    { kStress,  kCtlKnob,  500, kBandKnobY, 22, 0, kLampNone, "STRESS" },
    { kB1Level, kCtlKnob,  610, kBandKnobY, 22, 0, kLampNone,  "BAND 1 LEVEL" },
    { kB1On,    kCtlLamp,  680, kBandKnobY, 34, 30, kLampAmber, "BAND 1" },
    { kB2Level, kCtlKnob,  760, kBandKnobY, 22, 0, kLampNone,  "BAND 2 LEVEL" },
    { kB2On,    kCtlLamp,  830, kBandKnobY, 34, 30, kLampAmber, "BAND 2" },
    { kB3Level, kCtlKnob,  910, kBandKnobY, 22, 0, kLampNone,  "BAND 3 LEVEL" },
    { kB3On,    kCtlLamp,  980, kBandKnobY, 34, 30, kLampAmber, "BAND 3" },
    { kB4Level, kCtlKnob, 1060, kBandKnobY, 22, 0, kLampNone,  "BAND 4 LEVEL" },
    { kB4On,    kCtlLamp, 1130, kBandKnobY, 34, 30, kLampAmber, "BAND 4" },
    // Trimpots by the module
    { kInput,   kCtlTrim, 1270, 54,  12, 0, kLampNone, "INPUT" },
    { kOutput,  kCtlTrim, 1270, 116, 12, 0, kLampNone, "OUTPUT" },
};
const int kNumCtls = (int) (sizeof (kCtls) / sizeof (kCtls[0]));

const char* const kBandFreq[4] = { "below 80 Hz", "80 Hz - 3 kHz", "above 3 kHz", "above 9 kHz" };

// Preset section in the strip: a SAVE button and slots 1..8, then the readout
const CGFloat kStripLabelY = 181.0;
const NSRect kSaveRect = { { 30.0, 201.0 }, { 46.0, 26.0 } };
const CGFloat kSlotX0 = 84.0, kSlotPitch = 33.0, kSlotW = 29.0, kSlotY = 201.0, kSlotH = 26.0;
const NSRect kReadoutRect = { { 380.0, 190.0 }, { 640.0, 50.0 } };
const NSRect kRecLamp = { { 388.0, 83.0 }, { 54.0, 46.0 } };     // always lit

inline NSRect slotRect (int k) { return NSMakeRect (kSlotX0 + k * kSlotPitch, kSlotY, kSlotW, kSlotH); }
inline NSRect ctlRect (const Ctl& c) { return NSMakeRect (c.cx - c.w * 0.5, c.cy - c.h * 0.5, c.w, c.h); }

// Pointer angle (radians, 0 = up, clockwise positive) for a 0..1 value.
inline CGFloat valueAngle (CGFloat v) { return (CGFloat) ((-150.0 + 300.0 * v) * M_PI / 180.0); }

// Point at angle a (0 = up, clockwise) and radius r around (cx, cy) in flipped coordinates.
inline NSPoint polar (CGFloat cx, CGFloat cy, CGFloat r, CGFloat a)
{
    return NSMakePoint (cx + r * std::sin (a), cy - r * std::cos (a));
}

NSColor* rgb (int r, int g, int b) { return [NSColor colorWithCalibratedRed: r / 255.0 green: g / 255.0 blue: b / 255.0 alpha: 1.0]; }

NSFont* boldFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica-Bold" size: size];
    return f ? f : [NSFont boldSystemFontOfSize: size];
}

NSFont* plainFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica" size: size];
    return f ? f : [NSFont systemFontOfSize: size];
}

NSDictionary* textAttrs (NSFont* font, NSColor* color, CGFloat kern)
{
    return [NSDictionary dictionaryWithObjectsAndKeys:
              font, NSFontAttributeName, color, NSForegroundColorAttributeName,
              [NSNumber numberWithDouble: kern], NSKernAttributeName, nil];
}

void drawText (const char* s, NSFont* font, NSColor* color, CGFloat cx, CGFloat cy, CGFloat kern = 0.0)
{
    NSString* str = [NSString stringWithUTF8String: s];
    NSDictionary* attrs = textAttrs (font, color, kern);
    const NSSize sz = [str sizeWithAttributes: attrs];
    [str drawAtPoint: NSMakePoint (cx - (sz.width - kern) * 0.5, cy - sz.height * 0.5) withAttributes: attrs];
}

// Lamp colours: lit cap (top, bottom) and glow; unlit cap
void lampColours (int lamp, NSColor** top, NSColor** bottom, NSColor** glow, NSColor** dimTop, NSColor** dimBottom)
{
    switch (lamp)
    {
        case kLampRed:
            *top = rgb (255, 120, 100); *bottom = rgb (220, 30, 24); *glow = rgb (255, 60, 40);
            *dimTop = rgb (120, 52, 50); *dimBottom = rgb (72, 22, 22); break;
        case kLampGreen:
            *top = rgb (120, 255, 170); *bottom = rgb (20, 190, 90); *glow = rgb (60, 255, 130);
            *dimTop = rgb (46, 104, 78); *dimBottom = rgb (18, 58, 40); break;
        case kLampAmber:
            *top = rgb (255, 236, 150); *bottom = rgb (255, 170, 30); *glow = rgb (255, 190, 60);
            *dimTop = rgb (122, 108, 74); *dimBottom = rgb (72, 60, 36); break;
        default:    // white / clear
            *top = rgb (255, 255, 250); *bottom = rgb (236, 236, 226); *glow = rgb (255, 255, 235);
            *dimTop = rgb (176, 176, 170); *dimBottom = rgb (118, 118, 114); break;
    }
}

} // namespace

//==============================================================================
@interface TBTypeA3602026PanelView : NSView
{
    EditorHost* host;
    float shown[kNumParams];
    float meter[kNumMeters];
    NSTimer* timer;
    int dragCtl;
    CGFloat dragStartY, dragStartValue;
    int readoutParam;
    int readoutCtl;
    bool saveMode;
    int activePreset;
    bool presetUsed[kNumPresetSlots];
    char status[64];
}
- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h;
- (void) shutdown;
- (CGFloat) meterAngle: (double) dB;
@end

@implementation TBTypeA3602026PanelView

- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h
{
    self = [super initWithFrame: frame];
    if (self)
    {
        host = h;
        for (int i = 0; i < kNumParams; ++i) shown[i] = host->getParam (i);
        for (int i = 0; i < kNumMeters; ++i) meter[i] = host->getMeter (i);
        dragCtl = -1;
        readoutParam = -1;
        readoutCtl = -1;
        saveMode = false;
        activePreset = -1;
        status[0] = 0;
        host->refreshPresets();
        for (int k = 0; k < kNumPresetSlots; ++k) presetUsed[k] = host->presetExists (k);
        timer = [[NSTimer timerWithTimeInterval: 1.0 / 20.0 target: self selector: @selector (tick:)
                                       userInfo: nil repeats: YES] retain];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSDefaultRunLoopMode];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSEventTrackingRunLoopMode];
    }
    return self;
}

- (void) shutdown
{
    if (timer) { [timer invalidate]; [timer release]; timer = nil; }
    host = 0;
}

- (void) dealloc
{
    [self shutdown];
    [super dealloc];
}

- (BOOL) isFlipped { return YES; }
- (BOOL) isOpaque { return YES; }
- (BOOL) acceptsFirstMouse: (NSEvent*) e { (void) e; return YES; }

// Host-side changes (automation, presets) and the meter redraw the panel.
- (void) tick: (NSTimer*) t
{
    (void) t;
    if (! host) return;
    bool changed = false, moved = false;
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = host->getParam (i);
        if (v != shown[i]) { shown[i] = v; changed = true; }
    }
    for (int i = 0; i < kNumMeters; ++i)
    {
        const float v = host->getMeter (i);
        if (std::fabs (v - meter[i]) > 0.05f && (v > -40.0f || meter[i] > -40.0f)) { meter[i] = v; moved = true; }
    }
    if (changed) [self setNeedsDisplay: YES];
    else if (moved) [self setNeedsDisplayInRect: NSInsetRect (kMeterFace, -8.0, -8.0)];
}

//------------------------------------------------------------------------------
// Drawing

// Brushed aluminium: a gradient with fine horizontal streaks
- (void) fillBrushed: (NSRect) r light: (int) light dark: (int) dark
{
    NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (light, light + 2, light + 4)
                                                   endingColor: rgb (dark, dark + 3, dark + 6)] autorelease];
    [g drawInRect: r angle: 90.0];
    unsigned int seed = (unsigned int) (r.origin.x * 7.0 + r.origin.y * 13.0) + 99u;
    for (CGFloat y = r.origin.y + 1.0; y < NSMaxY (r) - 1.0; y += 1.5)
    {
        seed = seed * 1664525u + 1013904223u;
        const CGFloat a = ((seed >> 20) & 0xFF) / 255.0 * 0.06;
        [[NSColor colorWithCalibratedWhite: ((seed >> 12) & 1) ? 1.0 : 0.0 alpha: a] set];
        NSRectFillUsingOperation (NSMakeRect (r.origin.x, y, r.size.width, 0.8), NSCompositeSourceOver);
    }
}

- (void) drawEarSlotAt: (NSPoint) c
{
    NSRect r = NSMakeRect (c.x - 17.0, c.y - 7.0, 34.0, 14.0);
    NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect: r xRadius: 7.0 yRadius: 7.0];
    NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (40, 41, 43) endingColor: rgb (88, 90, 94)] autorelease];
    [g drawInBezierPath: p angle: 90.0];
    [rgb (150, 152, 156) set];
    [p setLineWidth: 0.8];
    [p stroke];
}

// Chrome knurled thumbscrew on the module cover
- (void) drawThumbscrewAt: (NSPoint) c
{
    const CGFloat r = 11.0;
    const int ridges = 28;
    for (int k = 0; k < ridges; ++k)
    {
        const CGFloat a0 = (CGFloat) (2.0 * M_PI * k / ridges), a1 = (CGFloat) (2.0 * M_PI * (k + 0.5) / ridges);
        NSBezierPath* w = [NSBezierPath bezierPath];
        [w moveToPoint: c];
        [w lineToPoint: polar (c.x, c.y, r, a0)];
        [w lineToPoint: polar (c.x, c.y, r, a1)];
        [w closePath];
        const CGFloat lit = 0.5 + 0.5 * std::cos (a0 + 0.8);
        [[NSColor colorWithCalibratedWhite: 0.45 + 0.4 * lit alpha: 1.0] set];
        [w fill];
    }
    const CGFloat rc = 7.0;
    NSBezierPath* cap = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - rc, c.y - rc, 2 * rc, 2 * rc)];
    NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (245, 246, 248) endingColor: rgb (120, 122, 126)] autorelease];
    [g drawInBezierPath: cap relativeCenterPosition: NSMakePoint (-0.35, 0.4)];
    [rgb (60, 62, 66) set];
    [cap setLineWidth: 0.8];
    [cap stroke];
}

- (void) drawFace
{
    const NSRect b = [self bounds];
    // Rack ears and the face behind the plates
    [self fillBrushed: NSMakeRect (0, 0, b.size.width, kFaceH) light: 232 dark: 196];
    [rgb (120, 122, 126) set];
    NSRectFill (NSMakeRect (0, 0, b.size.width, 1.0));
    [self drawEarSlotAt: NSMakePoint (30, 30)];
    [self drawEarSlotAt: NSMakePoint (30, 136)];
    [self drawEarSlotAt: NSMakePoint (b.size.width - 30, 30)];
    [self drawEarSlotAt: NSMakePoint (b.size.width - 30, 136)];

    // Silver control plate in its dark frame
    [rgb (70, 72, 76) set];
    NSRectFill (NSInsetRect (kSilverBox, -3.0, -3.0));
    [self fillBrushed: kSilverBox light: 226 dark: 202];

    // Black module cover
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [NSGraphicsContext saveGraphicsState];
    [sh setShadowOffset: NSMakeSize (1.5, -2.0)];
    [sh setShadowBlurRadius: 3.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.5]];
    [sh set];
    [rgb (20, 20, 22) set];
    NSRectFill (kBlackBox);
    [NSGraphicsContext restoreGraphicsState];
    NSGradient* sheen = [[[NSGradient alloc] initWithStartingColor: rgb (34, 34, 37) endingColor: rgb (14, 14, 16)] autorelease];
    [sheen drawInRect: kBlackBox angle: 90.0];
    [self drawThumbscrewAt: NSMakePoint (NSMinX (kBlackBox) + 26.0, NSMidY (kBlackBox))];
    [self drawThumbscrewAt: NSMakePoint (NSMaxX (kBlackBox) - 26.0, NSMidY (kBlackBox))];

    // TB electronic logo (white "electronic" on the black), plain red stripe, red box
    tblogo::drawTBLogo (NSMakeRect (1300.0, NSMidY (kBlackBox) - 36.0, 130.0, 72.0), rgb (236, 236, 236));
    NSColor* red = rgb (198, 52, 36);
    const NSRect box = NSMakeRect (1560.0, 27.0, 128.0, 112.0);
    [red set];
    NSRectFill (NSMakeRect (1438.0, NSMidY (kBlackBox) - 11.0, NSMinX (box) - 1438.0 + 2.0, 22.0));
    NSBezierPath* bp = [NSBezierPath bezierPathWithRect: NSInsetRect (box, 3.5, 3.5)];
    [bp setLineWidth: 7.0];
    [bp stroke];

    // Labels on the silver plate (black, plain lettering as on the unit)
    NSColor* ink = rgb (20, 20, 22);
    drawText ("Dolby tone", boldFont (11.0), ink, 190, 24);
    drawText ("NR", boldFont (11.5), ink, 345, 37, 2.0);
    drawText ("(in - out)", plainFont (10.0), ink, 345, 51);
    drawText ("rec", boldFont (11.5), ink, 415, 48);
    drawText ("STRESS", boldFont (13.0), ink, 500, 24, 0.8);
    drawText ("all bands", plainFont (10.0), ink, 500, 52);
    drawText ("0 to +24 dB", plainFont (11.0), ink, 500, 142);
    NSBezierPath* br = [NSBezierPath bezierPath];
    [br moveToPoint: NSMakePoint (393.0, 59.5)]; [br lineToPoint: NSMakePoint (437.0, 59.5)];
    [br setLineWidth: 1.2];
    [ink set];
    [br stroke];

    // Trimpot labels on the module cover
    NSColor* white = rgb (236, 236, 236);
    drawText ("input", plainFont (10.0), white, 1270, 29);
    drawText ("output", plainFont (10.0), white, 1270, 91);

    // Divider before the band mod
    [rgb (60, 62, 66) set];
    NSRectFill (NSMakeRect (558.0, 22.0, 1.2, 122.0));
    [rgb (246, 247, 249) set];
    NSRectFill (NSMakeRect (559.2, 22.0, 0.8, 122.0));
    for (int b = 0; b < 4; ++b)
    {
        char t[16];
        std::snprintf (t, sizeof (t), "BAND %d", b + 1);
        const CGFloat x = bandX (b);
        drawText (t, boldFont (13.0), ink, x, 24, 0.8);
        drawText ("level", plainFont (10.0), ink, x - 30.0, 52);
        drawText ("on", plainFont (10.0), ink, x + 40.0, 62);
        drawText (kBandFreq[b], plainFont (11.0), ink, x, 142);
    }
    // Meter's zero-set screw
    [rgb (60, 62, 66) set];
    [[NSBezierPath bezierPathWithOvalInRect: NSMakeRect (187.0, 138.0, 6.0, 6.0)] fill];
}

- (void) drawStrip
{
    const NSRect b = [self bounds];
    NSGradient* strip = [[[NSGradient alloc] initWithStartingColor: rgb (196, 199, 203)
                                                       endingColor: rgb (166, 170, 175)] autorelease];
    [strip drawInRect: NSMakeRect (0, kStripTop, b.size.width, b.size.height - kStripTop) angle: 90.0];
    [rgb (40, 40, 42) set];
    NSRectFill (NSMakeRect (0, kStripTop, b.size.width, 2.0));
    [rgb (238, 240, 242) set];
    NSRectFill (NSMakeRect (0, kStripTop + 2.0, b.size.width, 1.0));
    drawText ("TB TYPE A 360", boldFont (22.0), [NSColor blackColor], 1470.0, 214.0, 2.0);
}

// The small Dolby-level meter: cream face, scale -20 .. +5 dB re Dolby level
- (void) drawMeter
{
    const NSRect f = kMeterFace;
    // Bezel
    NSBezierPath* bez = [NSBezierPath bezierPathWithRoundedRect: NSInsetRect (f, -5.0, -5.0) xRadius: 3 yRadius: 3];
    NSGradient* bg = [[[NSGradient alloc] initWithStartingColor: rgb (250, 250, 252) endingColor: rgb (130, 132, 136)] autorelease];
    [bg drawInBezierPath: bez angle: -90.0];
    [rgb (70, 72, 76) set];
    [bez setLineWidth: 1.0];
    [bez stroke];
    NSGradient* face = [[[NSGradient alloc] initWithStartingColor: rgb (250, 247, 236) endingColor: rgb (226, 222, 208)] autorelease];
    [face drawInRect: f angle: 90.0];

    [NSGraphicsContext saveGraphicsState];
    NSRectClip (f);
    const CGFloat px = kMeterPivot.x, py = kMeterPivot.y;
    struct Mark { double dB; const char* text; };
    const Mark marks[] = { { -20, "20" }, { -10, "10" }, { -7, "" }, { -5, "5" }, { -3, "3" }, { -2, "" },
                           { -1, "" }, { 0, "0" }, { 1, "" }, { 2, "" }, { 3, "+3" }, { 5, "+5" } };
    NSColor* ink = rgb (24, 24, 26);
    // Red arc above Dolby level
    {
        const CGFloat a0 = [self meterAngle: 0.0], a1 = [self meterAngle: 5.0];
        NSBezierPath* arc = [NSBezierPath bezierPath];
        [arc appendBezierPathWithArcWithCenter: NSMakePoint (px, py) radius: 75.0
                                    startAngle: (CGFloat) (a0 * 180.0 / M_PI - 90.0)
                                      endAngle: (CGFloat) (a1 * 180.0 / M_PI - 90.0) clockwise: NO];
        [arc setLineWidth: 3.5];
        [rgb (210, 40, 30) set];
        [arc stroke];
    }
    for (size_t k = 0; k < sizeof (marks) / sizeof (marks[0]); ++k)
    {
        const CGFloat a = [self meterAngle: marks[k].dB];
        const bool big = marks[k].text[0] != 0;
        NSBezierPath* t = [NSBezierPath bezierPath];
        [t moveToPoint: polar (px, py, big ? 69.0 : 72.0, a)];
        [t lineToPoint: polar (px, py, 79.0, a)];
        [t setLineWidth: big ? 1.3 : 0.9];
        [(marks[k].dB > 0.0 ? rgb (200, 30, 24) : ink) set];
        [t stroke];
        if (big)
        {
            const NSPoint p = polar (px, py, 87.0, a);
            drawText (marks[k].text, boldFont (7.5), marks[k].dB > 0.0 ? rgb (200, 30, 24) : ink, p.x, p.y);
        }
    }
    drawText ("Dolby level", plainFont (7.5), ink, px, 118.0);

    // Needle
    double dB = meter[kMeterLevel];
    const CGFloat a = [self meterAngle: dB];
    NSBezierPath* n = [NSBezierPath bezierPath];
    [n moveToPoint: polar (px, py, 30.0, a)];
    [n lineToPoint: polar (px, py, 84.0, a)];
    [n setLineWidth: 1.4];
    [rgb (16, 16, 18) set];
    [n stroke];
    [NSGraphicsContext restoreGraphicsState];

    // Glass sheen
    NSGradient* glass = [[[NSGradient alloc] initWithStartingColor: [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.25]
                                                       endingColor: [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.0]] autorelease];
    [glass drawInRect: NSMakeRect (f.origin.x, f.origin.y, f.size.width, f.size.height * 0.45) angle: -90.0];
}

// Needle angle for a level in dB re Dolby level: linear in volts, -20 dB at
// the left stop (-40 degrees), +5 dB at the right (+40 degrees)
- (CGFloat) meterAngle: (double) dB
{
    const double lo = std::pow (10.0, -20.0 / 20.0), hi = std::pow (10.0, 5.0 / 20.0);
    double p = (std::pow (10.0, dB / 20.0) - lo) / (hi - lo);
    if (p < -0.04) p = -0.04;
    if (p > 1.06) p = 1.06;
    return (CGFloat) ((-40.0 + 80.0 * p) * M_PI / 180.0);
}

// Square lamp pushbutton: lit cap glows in its colour, unlit cap dark.
- (void) drawLampButton: (NSRect) r lamp: (int) lamp lit: (bool) lit
{
    // Bezel
    NSBezierPath* bez = [NSBezierPath bezierPathWithRoundedRect: NSInsetRect (r, -3.0, -3.0) xRadius: 3 yRadius: 3];
    NSGradient* bg = [[[NSGradient alloc] initWithStartingColor: rgb (70, 72, 76) endingColor: rgb (24, 24, 26)] autorelease];
    [bg drawInBezierPath: bez angle: -90.0];

    NSColor *top, *bottom, *glow, *dimTop, *dimBottom;
    lampColours (lamp, &top, &bottom, &glow, &dimTop, &dimBottom);
    NSBezierPath* cap = [NSBezierPath bezierPathWithRoundedRect: r xRadius: 2 yRadius: 2];
    if (lit)
    {
        [NSGraphicsContext saveGraphicsState];
        NSShadow* g = [[[NSShadow alloc] init] autorelease];
        [g setShadowOffset: NSMakeSize (0.0, 0.0)];
        [g setShadowBlurRadius: 12.0];
        [g setShadowColor: glow];
        [g set];
        [bottom set];
        [cap fill];
        [cap fill];
        [NSGraphicsContext restoreGraphicsState];
        NSGradient* lg = [[[NSGradient alloc] initWithStartingColor: top endingColor: bottom] autorelease];
        [lg drawInBezierPath: cap relativeCenterPosition: NSMakePoint (0.0, 0.15)];
    }
    else
    {
        NSGradient* dg = [[[NSGradient alloc] initWithStartingColor: dimTop endingColor: dimBottom] autorelease];
        [dg drawInBezierPath: cap angle: -90.0];
    }
    // Lens facets: a soft highlight across the top
    NSGradient* hl = [[[NSGradient alloc] initWithStartingColor: [NSColor colorWithCalibratedWhite: 1.0 alpha: lit ? 0.45 : 0.22]
                                                    endingColor: [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.0]] autorelease];
    [hl drawInRect: NSMakeRect (r.origin.x + 2.0, r.origin.y + 2.0, r.size.width - 4.0, r.size.height * 0.45) angle: -90.0];
    [rgb (20, 20, 22) set];
    [cap setLineWidth: 1.0];
    [cap stroke];
}

// The Dolby tone button: a black cap; it glows amber round its edge while the tone runs.
- (void) drawToneButton: (const Ctl&) c
{
    const NSRect r = ctlRect (c);
    const bool lit = isOn (shown[c.param]);
    NSBezierPath* cap = [NSBezierPath bezierPathWithRoundedRect: r xRadius: 2 yRadius: 2];
    [NSGraphicsContext saveGraphicsState];
    NSShadow* g = [[[NSShadow alloc] init] autorelease];
    [g setShadowOffset: lit ? NSMakeSize (0.0, 0.0) : NSMakeSize (1.0, -2.0)];
    [g setShadowBlurRadius: lit ? 12.0 : 2.5];
    [g setShadowColor: lit ? rgb (255, 180, 50) : [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.5]];
    [g set];
    [rgb (16, 16, 18) set];
    [cap fill];
    if (lit) [cap fill];
    [NSGraphicsContext restoreGraphicsState];
    NSGradient* cg = [[[NSGradient alloc] initWithStartingColor: rgb (70, 70, 74) endingColor: rgb (12, 12, 14)] autorelease];
    [cg drawInBezierPath: cap angle: -90.0];
    if (lit)
    {
        [rgb (255, 190, 70) set];
        [cap setLineWidth: 1.6];
        [cap stroke];
    }
}

// Grey knurled knob with a red position line.
- (void) drawKnobAt: (NSPoint) ctr radius: (CGFloat) r value: (CGFloat) v
{
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (1.5, -2.5)];
    [sh setShadowBlurRadius: 4.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.45]];
    [sh set];
    [rgb (90, 93, 97) set];
    [[NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - r, ctr.y - r, 2 * r, 2 * r)] fill];
    [NSGraphicsContext restoreGraphicsState];

    const int ridges = 40;
    for (int k = 0; k < ridges; ++k)
    {
        const CGFloat a0 = (CGFloat) (2.0 * M_PI * k / ridges);
        const CGFloat a1 = (CGFloat) (2.0 * M_PI * (k + 0.5) / ridges);
        NSBezierPath* w = [NSBezierPath bezierPath];
        [w moveToPoint: ctr];
        [w lineToPoint: polar (ctr.x, ctr.y, r, a0)];
        [w lineToPoint: polar (ctr.x, ctr.y, r, a1)];
        [w closePath];
        const CGFloat lit = 0.5 + 0.5 * std::cos (a0 + 0.8);
        [[NSColor colorWithCalibratedWhite: 0.40 + 0.22 * lit alpha: 1.0] set];
        [w fill];
    }

    const CGFloat rc = r * 0.78;
    NSGradient* cap = [[[NSGradient alloc] initWithStartingColor: rgb (172, 175, 179)
                                                     endingColor: rgb (104, 107, 111)] autorelease];
    NSBezierPath* capPath = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - rc, ctr.y - rc, 2 * rc, 2 * rc)];
    [cap drawInBezierPath: capPath angle: -60.0];
    [rgb (70, 72, 75) set];
    [capPath setLineWidth: 0.8];
    [capPath stroke];

    const CGFloat a = valueAngle (v);
    NSBezierPath* line = [NSBezierPath bezierPath];
    [line moveToPoint: polar (ctr.x, ctr.y, r * 0.18, a)];
    [line lineToPoint: polar (ctr.x, ctr.y, r * 0.98, a)];
    [line setLineWidth: 2.4];
    [line setLineCapStyle: NSRoundLineCapStyle];
    [rgb (200, 30, 30) set];
    [line stroke];
}

// Screwdriver trimpot on the module cover: silver body with a slot, end ticks
- (void) drawTrim: (const Ctl& ) c
{
    const NSPoint ctr = NSMakePoint (c.cx, c.cy);
    const CGFloat r = c.w;
    NSColor* white = rgb (210, 210, 212);
    for (int k = 0; k <= 10; k += 5)
    {
        const CGFloat a = valueAngle (k / 10.0);
        NSBezierPath* t = [NSBezierPath bezierPath];
        [t moveToPoint: polar (ctr.x, ctr.y, r + 3.0, a)];
        [t lineToPoint: polar (ctr.x, ctr.y, r + 7.0, a)];
        [t setLineWidth: 1.2];
        [white set];
        [t stroke];
    }
    const NSPoint pm = polar (ctr.x, ctr.y, r + 12.0, valueAngle (0.0));
    const NSPoint pp = polar (ctr.x, ctr.y, r + 12.0, valueAngle (1.0));
    drawText ("-", boldFont (10.0), white, pm.x, pm.y);
    drawText ("+", boldFont (10.0), white, pp.x, pp.y);

    NSBezierPath* body = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - r, ctr.y - r, 2 * r, 2 * r)];
    NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (238, 239, 241) endingColor: rgb (126, 128, 132)] autorelease];
    [g drawInBezierPath: body relativeCenterPosition: NSMakePoint (-0.35, 0.4)];
    [rgb (8, 8, 10) set];
    [body setLineWidth: 1.2];
    [body stroke];
    const CGFloat a = valueAngle (shown[c.param]);
    NSBezierPath* slot = [NSBezierPath bezierPath];
    [slot moveToPoint: polar (ctr.x, ctr.y, r * 0.85, a + (CGFloat) M_PI)];
    [slot lineToPoint: polar (ctr.x, ctr.y, r * 0.85, a)];
    [slot setLineWidth: 3.0];
    [rgb (40, 40, 44) set];
    [slot stroke];
    // A red dot on the slot's pointing end
    const NSPoint d = polar (ctr.x, ctr.y, r * 0.62, a);
    [rgb (210, 40, 30) set];
    [[NSBezierPath bezierPathWithOvalInRect: NSMakeRect (d.x - 1.8, d.y - 1.8, 3.6, 3.6)] fill];
}

// Band level knob with its 0..10 tick scale (numbered at the ends only)
- (void) drawBandKnob: (const Ctl&) c
{
    NSColor* ink = rgb (20, 20, 22);
    for (int k = 0; k <= 10; ++k)
    {
        const CGFloat a = valueAngle (k / 10.0);
        NSBezierPath* t = [NSBezierPath bezierPath];
        [t moveToPoint: polar (c.cx, c.cy, c.w + 3.0, a)];
        [t lineToPoint: polar (c.cx, c.cy, c.w + ((k % 5) == 0 ? 8.0 : 6.0), a)];
        [t setLineWidth: (k % 5) == 0 ? 1.3 : 0.9];
        [ink set];
        [t stroke];
    }
    if (c.param != kStress)
    {
        // Stock summing resistor at 5: a red tick
        NSBezierPath* st = [NSBezierPath bezierPath];
        [st moveToPoint: polar (c.cx, c.cy, c.w + 3.0, 0.0)];
        [st lineToPoint: polar (c.cx, c.cy, c.w + 9.0, 0.0)];
        [st setLineWidth: 2.2];
        [rgb (200, 30, 24) set];
        [st stroke];
    }
    const NSPoint p0 = polar (c.cx, c.cy, c.w + 15.0, valueAngle (0.0));
    const NSPoint p1 = polar (c.cx, c.cy, c.w + 15.0, valueAngle (1.0));
    drawText ("0", boldFont (9.5), ink, p0.x, p0.y);
    drawText ("10", boldFont (9.5), ink, p1.x, p1.y);
    [self drawKnobAt: NSMakePoint (c.cx, c.cy) radius: c.w value: shown[c.param]];
}

- (void) drawReadout
{
    const NSRect box = kReadoutRect;
    NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect: box xRadius: 4 yRadius: 4];
    NSGradient* glow = [[[NSGradient alloc] initWithStartingColor: rgb (206, 255, 120)
                                                      endingColor: rgb (150, 232, 70)] autorelease];
    [glow drawInBezierPath: p relativeCenterPosition: NSMakePoint (0.0, 0.0)];
    [rgb (60, 62, 65) set];
    [p setLineWidth: 1.0];
    [p stroke];

    if (status[0])
        drawText (status, boldFont (16.0), [NSColor blackColor], NSMidX (box), NSMidY (box));
    else if (readoutParam >= 0 && host)
    {
        char disp[16] = { 0 };
        host->getParamDisplay (readoutParam, disp);
        const int kind = kParamKind[readoutParam];
        const char* name = (readoutCtl >= 0) ? kCtls[readoutCtl].label : kParamNames[readoutParam];
        char text[96];
        const bool trim = (kind == kInTrim || kind == kOutTrim);
        const bool db = trim || kind == kStressKnob || (kind == kBandLevel && std::strcmp (disp, "Off") != 0);
        // Upper-case the switch states for the readout
        for (char* q = disp; *q; ++q) if (*q >= 'a' && *q <= 'z' && kind != kBandLevel && kind != kStressKnob && ! trim) *q = (char) (*q - 32);
        std::snprintf (text, sizeof (text), "%s   %s%s", name, disp, db ? " dB" : "");
        drawText (text, boldFont (16.0), [NSColor blackColor], NSMidX (box), NSMidY (box));
    }
}

- (void) drawPresets
{
    NSColor* ink = [NSColor blackColor];
    drawText ("PRESETS", boldFont (10.0), ink, (NSMinX (kSaveRect) + NSMaxX (slotRect (kNumPresetSlots - 1))) * 0.5, kStripLabelY);

    NSBezierPath* sp = [NSBezierPath bezierPathWithRoundedRect: kSaveRect xRadius: 3 yRadius: 3];
    if (saveMode)
    {
        [rgb (200, 30, 30) set];
        [sp fill];
    }
    else
    {
        NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (232, 234, 236) endingColor: rgb (160, 163, 167)] autorelease];
        [g drawInBezierPath: sp angle: -90.0];
    }
    [rgb (40, 41, 43) set];
    [sp setLineWidth: 1.0];
    [sp stroke];
    drawText ("SAVE", boldFont (11.0), saveMode ? [NSColor whiteColor] : ink, NSMidX (kSaveRect), NSMidY (kSaveRect));

    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        const NSRect r = slotRect (k);
        NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect: r xRadius: 3 yRadius: 3];
        NSColor* fill = (k == activePreset) ? rgb (178, 245, 96) : (presetUsed[k] ? rgb (46, 47, 50) : rgb (214, 216, 219));
        NSColor* text = (k == activePreset) ? ink : (presetUsed[k] ? [NSColor whiteColor] : rgb (120, 122, 126));
        [fill set];
        [bp fill];
        [rgb (40, 41, 43) set];
        [bp setLineWidth: 1.0];
        [bp stroke];
        char num[4];
        std::snprintf (num, sizeof (num), "%d", k + 1);
        drawText (num, boldFont (12.0), text, NSMidX (r), NSMidY (r));
    }
}

- (void) drawRect: (NSRect) dirty
{
    (void) dirty;
    [self drawFace];
    [self drawStrip];
    [self drawMeter];
    NSColor* ink = [NSColor blackColor];

    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        switch (c.type)
        {
            case kCtlKnob:  [self drawBandKnob: c]; break;
            case kCtlTone:  [self drawToneButton: c]; break;
            case kCtlLamp:  [self drawLampButton: ctlRect (c) lamp: c.lamp lit: isOn (shown[c.param])]; break;
            case kCtlTrim:  [self drawTrim: c]; break;
            default: break;
        }
    }
    [self drawLampButton: kRecLamp lamp: kLampRed lit: true];     // this unit only records
    [self drawPresets];
    [self drawReadout];
    (void) ink;
}

//------------------------------------------------------------------------------
// Mouse

- (int) ctlAtPoint: (NSPoint) p
{
    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        if (c.type == kCtlKnob || c.type == kCtlTrim)
        {
            const CGFloat reach = c.w + 6.0;
            const CGFloat dx = p.x - c.cx, dy = p.y - c.cy;
            if (dx * dx + dy * dy < reach * reach) return i;
        }
        else if (NSPointInRect (p, NSInsetRect (ctlRect (c), -4.0, -4.0)))
            return i;
    }
    return -1;
}

- (void) setParam: (int) param value: (float) v
{
    if (! host) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    host->guiSetParam (param, v);
    shown[param] = host->getParam (param);
    readoutParam = param;
    [self setNeedsDisplay: YES];
}

// SAVE button and preset slots. Returns YES if the click was handled here.
- (BOOL) presetClick: (NSPoint) p
{
    if (NSPointInRect (p, kSaveRect))
    {
        saveMode = ! saveMode;
        std::snprintf (status, sizeof (status), "%s", saveMode ? "SAVE: CLICK A SLOT 1-8" : "");
        [self setNeedsDisplay: YES];
        return YES;
    }
    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        if (! NSPointInRect (p, slotRect (k))) continue;
        if (saveMode)
        {
            const bool ok = host->savePreset (k);
            saveMode = false;
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (ok) activePreset = k;
            std::snprintf (status, sizeof (status), ok ? "PRESET %d SAVED" : "COULD NOT SAVE PRESET %d", k + 1);
        }
        else
        {
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (presetUsed[k] && host->loadPreset (k))
            {
                for (int j = 0; j < kNumParams; ++j) shown[j] = host->getParam (j);
                activePreset = k;
                std::snprintf (status, sizeof (status), "PRESET %d LOADED", k + 1);
            }
            else
                std::snprintf (status, sizeof (status), "PRESET %d IS EMPTY", k + 1);
        }
        [self setNeedsDisplay: YES];
        return YES;
    }
    return NO;
}

- (void) mouseDown: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    if (! host) return;
    if ([self presetClick: p]) return;
    const int i = [self ctlAtPoint: p];
    if (i < 0) return;
    const Ctl& c = kCtls[i];
    shown[c.param] = host->getParam (c.param);   // the live value, even between timer ticks
    readoutParam = c.param;
    readoutCtl = i;
    status[0] = 0;
    activePreset = -1;     // settings are being changed away from the preset

    if (c.type == kCtlLamp || c.type == kCtlTone)
    {
        // Lamp buttons toggle
        const float v = isOn (shown[c.param]) ? 0.0f : 1.0f;
        host->guiBeginEdit (c.param);
        [self setParam: c.param value: v];
        host->guiEndEdit (c.param);
        return;
    }

    host->guiBeginEdit (c.param);
    if ([e clickCount] == 2)
        [self setParam: c.param value: kDefaults[c.param]];   // double-click: back to default
    dragCtl = i;
    dragStartY = p.y;
    dragStartValue = shown[c.param];
    [self setNeedsDisplay: YES];
}

- (void) mouseDragged: (NSEvent*) e
{
    if (dragCtl < 0 || ! host) return;
    const Ctl& c = kCtls[dragCtl];
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const bool fine = ([e modifierFlags] & NSShiftKeyMask) != 0;
    const CGFloat perPixel = fine ? 1.0 / 1000.0 : 1.0 / 200.0;
    const CGFloat v = dragStartValue + (dragStartY - p.y) * perPixel;
    [self setParam: c.param value: (float) v];
}

- (void) mouseUp: (NSEvent*) e
{
    (void) e;
    if (dragCtl >= 0 && host) host->guiEndEdit (kCtls[dragCtl].param);
    dragCtl = -1;
}

- (void) scrollWheel: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const int i = [self ctlAtPoint: p];
    if (i < 0 || ! host) return;
    const Ctl& c = kCtls[i];
    if (c.type != kCtlKnob && c.type != kCtlTrim) return;
    const CGFloat d = [e deltaY];
    if (d == 0.0) return;
    status[0] = 0;
    activePreset = -1;
    readoutCtl = i;
    host->guiBeginEdit (c.param);
    [self setParam: c.param value: (float) (shown[c.param] + d * 0.01)];
    host->guiEndEdit (c.param);
}

@end

//==============================================================================
namespace tbtypea
{

// Both calls use their own autorelease pool, so nothing of this plugin's is
// left waiting in the host's pool when the editor closes or the plugin unloads.
void* editorOpen (void* parentView, EditorHost* host)
{
    NSView* parent = (NSView*) parentView;
    if (! parent || ! host) return 0;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    TBTypeA3602026PanelView* v = [[TBTypeA3602026PanelView alloc]
                                     initWithFrame: NSMakeRect (0, 0, kEditorWidth, kEditorHeight) host: host];
    if (v) [parent addSubview: v];
    [pool release];
    return (void*) v;    // retained until editorClose
}

void editorClose (void* handle)
{
    TBTypeA3602026PanelView* v = (TBTypeA3602026PanelView*) handle;
    if (! v) return;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [v shutdown];
    [v removeFromSuperview];
    [v release];
    [pool release];
}

} // namespace tbtypea
