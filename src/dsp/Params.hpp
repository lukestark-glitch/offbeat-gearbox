// Parameter von Offbeat Gearbox. Automatisierbar sind nur Play, BPM, Swing, Pattern und die Mutes; das Raster
// (Spuren, Schritte, Patterns) liegt im Plugin-State als JSON (Model.hpp). Nur hinten anhängen, nie umsortieren.
#pragma once

#include "ParamInfo.hpp"

namespace offbeat {

enum ParamId {
    kPlay,    // interner Play/Stop (wenn der Host nicht spielt)
    kBpm,     // internes Tempo (wenn der Host nicht spielt)
    kSwing,   // 0–75 %: ungerade 16tel später, 50 % = Triolen-Feel
    kPattern, // A–H, Wechsel zum nächsten Takt
    kMute1, kMute2, kMute3, kMute4, kMute5, kMute6, kMute7, kMute8,
    kFill,    // gedrückt halten: letzter Schlag jedes Takts als Rolle (versteckt)
    kRemote,  // iPad-Fernbedienung an/aus (versteckt)
    // Anzeige-Werte vom DSP an die Oberfläche (versteckt)
    kPlaying,
    kPlayingPattern,
    kQueuedPattern,
    kStep1, kStep2, kStep3, kStep4, kStep5, kStep6, kStep7, kStep8, // laufender Schritt je Spur, −1 = steht
    kRemotePort,
    kRemotePin,
    kRemoteClients,
    kRevision, // zählt bei jeder Änderung am Raster hoch (Oberfläche holt dann den Zustand)
    kMuteState, // Mutes, wie der DSP sie gerade anwendet (Bit je Spur): auch wenn der Host eine Änderung vom iPad nicht übernimmt
    kClockFollow, // folgt dem Takt von Offbeat Clutch (Shared Clock), wenn der Host nicht spielt (versteckt)
    kClockActive, // Anzeige: Takt von Clutch wird gerade verwendet (versteckt)
    kLinkIdHigh,  // Anzeige: Offbeat-Link-Kennung, obere 14 Bit (für die Oberfläche, versteckt)
    kLinkIdLow,   // Anzeige: untere 14 Bit
    kLoopBars,    // Loop (Slip): 0 aus, 1/2/4/8 Takte wiederholen, die Zeit läuft darunter weiter (versteckt)
    kRollSteps,   // Roll (Slip): 0 aus, 1/2/4 Schritte (1/16, 1/8, 1/4 Schlag) wiederholen; hat Vorrang vor Loop
    kParamCount
};

extern const ParamInfo kParams[kParamCount];

struct Preset {
    const char* name;
};
constexpr int kPresetCount = 1;
extern const Preset kPresets[kPresetCount];

void applyPreset(int index, float out[kParamCount]);
void applyDefaults(float out[kParamCount]);

// Parameter für KOPIEREN als JSON (das Raster kopiert die Oberfläche separat)
int jsonParams(const float values[kParamCount], int* out);

} // namespace offbeat
