// Editor.h
// Plain C++ interface between the VST wrapper and the Cocoa editor
// (PanelView.mm), so the wrapper itself stays free of Objective-C.
// The editor only exists on the Mac; elsewhere the plugin reports no editor.

#ifndef TBTYPEA_EDITOR_H
#define TBTYPEA_EDITOR_H

namespace tbtypea
{

// The panel is drawn on a 1800 x 270 canvas (the 1U rack face in 19" proportions,
// then the strip) and shown 20% smaller, 1440 x 216, so it fits a laptop screen.
enum { kPanelWidth = 1800, kPanelHeight = 270 };      // drawing coordinates
enum { kEditorWidth = 1440, kEditorHeight = 216 };    // window size the host sees

// What the panel shows from the running circuit
enum Meter { kMeterLevel = 0, kNumMeters };

// What the editor needs from the plugin.
class EditorHost
{
public:
    virtual ~EditorHost() {}
    virtual float getParam (int index) const = 0;
    virtual void  getParamDisplay (int index, char* text8) const = 0;   // up to 8 chars + NUL
    virtual void  guiBeginEdit (int index) = 0;
    virtual void  guiSetParam (int index, float value) = 0;           // also notifies the host
    virtual void  guiEndEdit (int index) = 0;
    // kMeterLevel: Output 1 in dB re Dolby level (VU-like ballistics)
    virtual float getMeter (int which) const = 0;

    // User presets (slots 0..kNumPresetSlots-1), stored in a file shared by every instance
    virtual void  refreshPresets() = 0;                 // re-read the file
    virtual bool  presetExists (int slot) const = 0;
    virtual bool  savePreset (int slot) = 0;            // current settings -> slot
    virtual bool  loadPreset (int slot) = 0;            // slot -> every parameter (host is told)
};

#if defined(__APPLE__)
// parentView is the host's NSView*. Returns an opaque handle, or 0 on failure.
void* editorOpen (void* parentView, EditorHost* host);
void  editorClose (void* handle);
#endif

} // namespace tbtypea

#endif
