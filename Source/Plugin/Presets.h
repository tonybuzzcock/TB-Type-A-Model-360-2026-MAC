// Presets.h
// Eight user preset slots, each holding every parameter, kept in one small
// text file so all instances and sessions share them:
//   Mac:   ~/Library/Application Support/TB/TB Type A 360/Presets.txt
//   other: ~/.config/TB/TB Type A 360/Presets.txt
// The TBTYPEA_PRESET_FILE environment variable overrides the location (tests).
// Plain C++98. File access happens only on the editor's thread, never in audio.

#ifndef TBTYPEA_PRESETS_H
#define TBTYPEA_PRESETS_H

#include "Params.h"

#include <string>

namespace tbtypea
{

enum { kNumPresetSlots = 8 };

class PresetStore
{
public:
    PresetStore();

    void setPath (const std::string& p) { path = p; }
    const std::string& getPath() const { return path; }
    static std::string defaultPath();

    bool load();                    // re-reads the file; missing file = all slots empty
    bool save() const;              // writes every slot, creating folders as needed

    bool has (int slot) const;
    bool get (int slot, float values[kNumParams]) const;
    void put (int slot, const float values[kNumParams]);

private:
    std::string path;
    bool used[kNumPresetSlots];
    float vals[kNumPresetSlots][kNumParams];
};

} // namespace tbtypea

#endif
