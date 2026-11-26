// Echtzeit-Teil von Offbeat Gearbox: erzeugt aus dem Raster (Model.hpp) sampelgenaue MIDI-Ereignisse.
// Kein Speicher anfordern, keine Sperren: das Raster kommt als fertiger Zeiger (setSet), den die
// Plugin-Hülle lock-frei austauscht. Ein Schritt = 1/16; Swing verschiebt ungerade Schritte um
// swing/75 · ½ Schritt (50 % ≈ Triolen-Feel). Pattern-Wechsel greift am nächsten Taktanfang (4/4).
#pragma once

#include "Model.hpp"
#include "Params.hpp"

#include <cstdint>

namespace offbeat {

struct MidiOut {
    uint32_t frame;
    uint8_t data[3];
};

class Sequencer {
public:
    Sequencer();

    void setSampleRate(float sr);
    void setParam(int id, float value);
    float getParam(int id) const { return params[id]; }

    // Raster für die folgenden Blöcke (bleibt im Besitz der Hülle; erst nach dem Tausch freigeben)
    void setSet(const Set* s) { set = s; }

    // Einmal pro Block vor process(): Host-Transport. hostPlaying mit gültigem Takt hat Vorrang vor Play.
    void setTransport(bool hostPlaying, double beat, double hostBpm);

    // Erzeugt die Ereignisse des Blocks, nach frame sortiert. Rückgabe: Anzahl (höchstens max).
    int process(uint32_t frames, MidiOut* out, int max);

    // Alle klingenden Noten aus und All-Notes-Off auf den benutzten Kanälen (z. B. beim Entladen)
    int panic(MidiOut* out, int max, uint32_t frame = 0);

private:
    struct Pending { // geplantes Ereignis in Schlägen
        double beat;
        uint8_t status, d1, d2;
        int8_t track; // für Note-Off: Spur; −1 = kein Note-Off
        bool active;
    };
    static constexpr int kMaxPending = 256;

    void stopAll(MidiOut* out, int& n, int max, uint32_t frame);
    void fireStep(int64_t g, double t, double b0, double bpf, uint32_t frames, MidiOut* out, int& n, int max);
    void schedule(double beat, uint8_t status, uint8_t d1, uint8_t d2, int track);
    void emit(MidiOut* out, int& n, int max, uint32_t frame, uint8_t s, uint8_t d1, uint8_t d2);
    double stepTime(int64_t g) const; // Beginn von Schritt g in Schlägen (mit Swing)
    void noteOff(int track, int note, MidiOut* out, int& n, int max, uint32_t frame);

    float params[kParamCount];
    float sr = 48000.0f;
    const Set* set = nullptr;

    // Transport
    bool hostPlaying = false, running = false, wasHost = false, playWasOn = false;
    double hostBeat = 0.0, hostTempo = 0.0, internalBeat = 0.0, expectedBeat = -1.0;
    int64_t lastFired = -1, patternStart = 0;
    int sentProgram[kTracks];
    bool chainActive = false;   // Ablauf läuft (seit Play-Start, bis ein Pattern-Knopf übernimmt)
    int64_t chainStart = 0;     // Schritt, an dem der Ablauf begann (Taktanfang)
    float lastPatternParam = 0; // erkennt Pattern-Knöpfe während des Ablaufs
    int chainWant(int64_t g) const; // zuletzt gesendeter Program Change je Spur (-1 = noch keiner)
    int playingPattern = 0;

    // klingende Noten je Spur: active[Spur][Note] = Kanal + 1 (0 = aus); mehrere bei Akkorden
    bool tieHold[kTracks] = {};
    uint8_t active[kTracks][128] = {};
    bool channelUsed[16] = {};
    Pending pending[kMaxPending];
};

} // namespace offbeat
