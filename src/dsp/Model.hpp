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

// Melodie: Tonhöhen je Schritt relativ zur Spur-Note, −24 … +23 Halbtöne (Bit k = Versatz k − 24).
constexpr int kKeyLow = -24, kKeyCount = 48, kMaxChord = 8;

struct Step {
    uint8_t vel = kVelOff;
    bool tie = false;  // Note hält über den nächsten Schritt (legato; andere Tonhöhe = Glide im Synth)
    uint64_t keys = 0; // gesetzte Bits = Noten des Schritts (Akkord); 0 = Spur-Note
};

inline uint64_t keyBit(int offset) { return offset >= kKeyLow && offset < kKeyLow + kKeyCount ? 1ull << (offset - kKeyLow) : 0; }

struct TrackConf { // gilt für alle Patterns
    char name[24] = "";
    int channel = 1;     // MIDI-Kanal 1–16
    int note = 60;       // MIDI-Note
    float gate = 0.5f;   // Notenlänge in Schritten (0,05–0,95)
    float volume = 1.0f; // skaliert die Velocity (0–1)
    bool solo = false;
    bool melody = false; // Melodie-Spur: Schritte mit eigenen Tonhöhen/Akkorden (Oberfläche: Tastatur)
    int synth = 0;       // nur für die Anzeige der Sound-Namen: 0 andere, 1 Engine, 2 Topline
};

// Sound-Namen (Presets) von Offbeat Engine/Topline in Programm-Reihenfolge – bei neuen Presets dort hier nachtragen
// (und in src/remote/page.html).
extern const char* const kEngineSounds[];
extern const char* const kToplineSounds[];
extern const int kEngineSoundCount, kToplineSoundCount;
const char* soundName(int synth, int program); // nullptr = unbekannt

struct PatternTrack {
    int length = 16; // Schrittzahl 1–64 (eigene Länge je Spur → Polymetrik)
    int program = -1; // Sound (MIDI Program Change 0–127) beim Start des Patterns; -1 = nicht senden
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
