#include "Params.hpp"

namespace offbeat {

namespace {
const char* const kPatternNames[] = { "A", "B", "C", "D", "E", "F", "G", "H" };
}

const ParamInfo kParams[kParamCount] = {
    // name            symbol          unit   min    max    default  flags
    { "Play",          "play",         "",    0,     1,     0,       kFlagBool },
    { "BPM",           "bpm",          "BPM", 60,    200,   160,     0 },
    { "Swing",         "swing",        "%",   0,     75,    0,       0 },
    { "Pattern",       "pattern",      "",    0,     7,     0,       kFlagInt, kPatternNames },
    { "Mute 1",        "mute1",        "",    0,     1,     0,       kFlagBool },
    { "Mute 2",        "mute2",        "",    0,     1,     0,       kFlagBool },
    { "Mute 3",        "mute3",        "",    0,     1,     0,       kFlagBool },
    { "Mute 4",        "mute4",        "",    0,     1,     0,       kFlagBool },
    { "Mute 5",        "mute5",        "",    0,     1,     0,       kFlagBool },
    { "Mute 6",        "mute6",        "",    0,     1,     0,       kFlagBool },
    { "Mute 7",        "mute7",        "",    0,     1,     0,       kFlagBool },
    { "Mute 8",        "mute8",        "",    0,     1,     0,       kFlagBool },
    { "Fill",          "fill",         "",    0,     1,     0,       kFlagBool | kFlagHidden },
    { "Remote",        "remote",       "",    0,     1,     0,       kFlagBool | kFlagHidden },
    { "Playing",       "playing",      "",    0,     1,     0,       kFlagOutput | kFlagHidden },
    { "Playing Pattern", "playing_pattern", "", 0,   7,     0,       kFlagOutput | kFlagHidden },
    { "Queued Pattern", "queued_pattern", "", 0,     7,     0,       kFlagOutput | kFlagHidden },
    { "Step 1",        "step1",        "",    -1,    63,    -1,      kFlagOutput | kFlagHidden },
    { "Step 2",        "step2",        "",    -1,    63,    -1,      kFlagOutput | kFlagHidden },
    { "Step 3",        "step3",        "",    -1,    63,    -1,      kFlagOutput | kFlagHidden },
    { "Step 4",        "step4",        "",    -1,    63,    -1,      kFlagOutput | kFlagHidden },
    { "Step 5",        "step5",        "",    -1,    63,    -1,      kFlagOutput | kFlagHidden },
    { "Step 6",        "step6",        "",    -1,    63,    -1,      kFlagOutput | kFlagHidden },
    { "Step 7",        "step7",        "",    -1,    63,    -1,      kFlagOutput | kFlagHidden },
    { "Step 8",        "step8",        "",    -1,    63,    -1,      kFlagOutput | kFlagHidden },
    { "Remote Port",   "remote_port",  "",    0,     65535, 0,       kFlagOutput | kFlagHidden },
    { "Remote PIN",    "remote_pin",   "",    0,     9999,  0,       kFlagOutput | kFlagHidden },
    { "Remote Clients", "remote_clients", "", 0,     16,    0,       kFlagOutput | kFlagHidden },
    { "Revision",      "revision",     "",    0,     1e7f,  0,       kFlagOutput | kFlagHidden },
    { "Mute State",    "mute_state",   "",    0,     255,   0,       kFlagOutput | kFlagHidden },
    { "Clock Follow",  "clock_follow", "",    0,     1,     1,       kFlagBool | kFlagHidden },
    { "Clock Active",  "clock_active", "",    0,     1,     0,       kFlagOutput | kFlagHidden },
    { "Link ID High",  "link_id_high", "",    0, 16383,     0,       kFlagOutput | kFlagHidden | kFlagInt },
    { "Link ID Low",   "link_id_low",  "",    0, 16383,     0,       kFlagOutput | kFlagHidden | kFlagInt },
    { "Loop Bars",     "loop_bars",    "",    0,     8,     0,       kFlagHidden | kFlagInt },
    { "Roll Steps",    "roll_steps",   "",    0,     4,     0,       kFlagHidden | kFlagInt },
};

const Preset kPresets[kPresetCount] = { { "Standard" } };

void applyDefaults(float out[kParamCount])
{
    for (int i = 0; i < kParamCount; ++i)
        out[i] = kParams[i].def;
}

void applyPreset(int, float out[kParamCount]) { applyDefaults(out); }

int jsonParams(const float[kParamCount], int* out)
{
    int n = 0;
    for (int p = 0; p < kParamCount; ++p)
        if (!(kParams[p].flags & (kFlagOutput | kFlagHidden)))
            out[n++] = p;
    return n;
}

} // namespace offbeat
