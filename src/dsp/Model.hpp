// Raster von Offbeat Gearbox: 8 Spuren, 8 Patterns (A–H), je Spur und Pattern bis zu 64 Schritte.
// Liegt im Plugin-State als JSON (setToJson/applyJsonText) und wird über Textbefehle geändert (applyCommand),
// dieselben Befehle nutzen Oberfläche und iPad-Fernbedienung. Nicht im Audio-Thread verwenden.
#pragma once

#include <cstdint>
#include <string>

namespace offbeat {

constexpr int kTracks = 8, kPatterns = 8, kMaxSteps = 64;

// Velocity-Stufen: 0 aus, 1 Ghost, 2 normal, 3 laut
enum Velocity : uint8_t { kVelOff, kVelGhost, kVelNormal, kVelLoud };
extern const int kVelocityValues[4]; // MIDI-Velocity je Stufe: 0, 50, 90, 110

struct Step {
    uint8_t vel = kVelOff;
    bool tie = false; // Note hält über den nächsten Schritt (legato, kein neuer Anschlag)
};

struct TrackConf { // gilt für alle Patterns
    char name[24] = "";
    int channel = 1;     // MIDI-Kanal 1–16
    int note = 60;       // MIDI-Note
    float gate = 0.5f;   // Notenlänge in Schritten (0,05–0,95)
    float volume = 1.0f; // skaliert die Velocity (0–1)
    bool solo = false;
};

struct PatternTrack {
    int length = 16; // Schrittzahl 1–64 (eigene Länge je Spur → Polymetrik)
    Step steps[kMaxSteps];
};

struct Pattern {
    PatternTrack tracks[kTracks];
};

struct Set {
    TrackConf tracks[kTracks];
    Pattern patterns[kPatterns];
};

void defaultSet(Set& s); // Kick, Bass, Hat … mit einem Bounce-Grundmuster in Pattern A

std::string setToJson(const Set& s);
std::string patternToJson(const Set& s, int pattern); // {"pattern":{...}} zum Kopieren eines Patterns

// Ganzes Set ({"gearbox":1,"tracks":…,"patterns":…}) oder ein Pattern ({"pattern":…}, ersetzt targetPattern)
bool applyJsonText(Set& s, const char* json, int targetPattern, std::string& err);

// Ein Befehl (eine Zeile). Raster-Befehle ändern s; Parameter-Befehle (mute/pattern/play/fill/swing/bpm)
// liefern paramId/paramValue zurück und lassen s unverändert.
enum class CommandResult { Edited, Param, Error };
CommandResult applyCommand(Set& s, const std::string& line, int& paramId, float& paramValue, std::string& err);

} // namespace offbeat
