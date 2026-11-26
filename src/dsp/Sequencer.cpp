#include "Sequencer.hpp"

#include <algorithm>
#include <cmath>

namespace offbeat {

namespace {
constexpr double kStep = 0.25; // Schlag pro Schritt (1/16)
constexpr int kBarSteps = 16;  // 4/4
}

Sequencer::Sequencer()
{
    applyDefaults(params);
    for (Pending& p : pending)
        p.active = false;
}

void Sequencer::setSampleRate(float newSr) { sr = newSr; }

void Sequencer::setParam(int id, float value)
{
    if (id < 0 || id >= kParamCount || (kParams[id].flags & kFlagOutput))
        return;
    params[id] = std::clamp(value, kParams[id].min, kParams[id].max);
}

void Sequencer::setTransport(bool playing, double beat, double bpm)
{
    hostPlaying = playing && bpm > 0.0;
    hostBeat = beat;
    hostTempo = bpm;
}

double Sequencer::stepTime(int64_t g) const
{
    const double swing = params[kSwing] / 75.0 * 0.5 * kStep; // höchstens ½ Schritt
    return (double)g * kStep + ((g & 1) ? swing : 0.0);
}

void Sequencer::emit(MidiOut* out, int& n, int max, uint32_t frame, uint8_t s, uint8_t d1, uint8_t d2)
{
    if (n >= max)
        return;
    out[n].frame = frame;
    out[n].data[0] = s, out[n].data[1] = d1, out[n].data[2] = d2;
    ++n;
}

void Sequencer::schedule(double beat, uint8_t status, uint8_t d1, uint8_t d2, int track)
{
    for (Pending& p : pending)
        if (!p.active) {
            p = { beat, status, d1, d2, (int8_t)track, true };
            return;
        }
}

void Sequencer::noteOff(int track, int note, MidiOut* out, int& n, int max, uint32_t frame)
{
    if (!active[track][note])
        return;
    emit(out, n, max, frame, (uint8_t)(0x80 | (active[track][note] - 1)), (uint8_t)note, 0);
    active[track][note] = 0;
    for (Pending& p : pending) // geplantes Ende dieser Note verwerfen
        if (p.active && p.track == track && p.d1 == note && (p.status & 0xf0) == 0x80)
            p.active = false;
}

void Sequencer::stopAll(MidiOut* out, int& n, int max, uint32_t frame)
{
    for (Pending& p : pending)
        p.active = false;
    for (int t = 0; t < kTracks; ++t) {
        for (int k = 0; k < 128; ++k)
            if (active[t][k]) {
                emit(out, n, max, frame, (uint8_t)(0x80 | (active[t][k] - 1)), (uint8_t)k, 0);
                active[t][k] = 0;
            }
        tieHold[t] = false;
    }
    for (int c = 0; c < 16; ++c)
        if (channelUsed[c]) {
            emit(out, n, max, frame, (uint8_t)(0xb0 | c), 123, 0); // All Notes Off („Panic“)
            channelUsed[c] = false;
        }
}

int Sequencer::panic(MidiOut* out, int max, uint32_t frame)
{
    int n = 0;
    stopAll(out, n, max, frame);
    return n;
}

void Sequencer::fireStep(int64_t g, double t, double b0, double bpf, uint32_t frames, MidiOut* out, int& n, int max)
{
    if (!set)
        return;
    if (g % kBarSteps == 0) { // Taktanfang: angefordertes Pattern übernehmen
        const int want = (int)params[kPattern];
        if (want != playingPattern) {
            playingPattern = want;
            patternStart = g;
        }
    }
    const uint32_t frame = std::min<uint32_t>(frames - 1, (uint32_t)std::max(0.0, std::floor((t - b0) / bpf + 0.5)));
    bool anySolo = false;
    for (const TrackConf& c : set->tracks)
        anySolo |= c.solo;
    const bool fill = params[kFill] >= 0.5f && (g % kBarSteps) >= kBarSteps - 4; // letzter Schlag des Takts
    for (int tr = 0; tr < kTracks; ++tr) {
        const TrackConf& c = set->tracks[tr];
        const PatternTrack& pt = set->patterns[playingPattern].tracks[tr];
        const int s = (int)(((g - patternStart) % pt.length + pt.length) % pt.length);
        params[kStep1 + tr] = (float)s;
        const Step& step = pt.steps[s];
        const bool audible = params[kMute1 + tr] < 0.5f && (!anySolo || c.solo);
        if (step.vel == kVelOff || !audible) {
            tieHold[tr] = false;
            continue;
        }
        const uint8_t ch = (uint8_t)(c.channel - 1);
        const int vel = std::clamp((int)std::lround(kVelocityValues[step.vel] * c.volume), 1, 127);
        const double len = (step.tie ? kStep : 0.0) + c.gate * kStep; // Tie: hält über den nächsten Schritt

        // Noten des Schritts: Spur-Note oder Akkord (höchstens kMaxChord, von unten)
        int notes[kMaxChord], count = 0;
        if (step.keys == 0)
            notes[count++] = c.note;
        for (int k = 0; k < kKeyCount && count < kMaxChord; ++k)
            if (step.keys >> k & 1) {
                const int note = c.note + k + kKeyLow;
                if (note >= 0 && note < 128)
                    notes[count++] = note;
            }
        auto inStep = [&](int note) {
            for (int i = 0; i < count; ++i)
                if (notes[i] == note)
                    return true;
            return false;
        };

        if (fill) { // Rolle: zwei 32tel, die zweite etwas lauter
            for (int k = 0; k < 128; ++k)
                noteOff(tr, k, out, n, max, frame);
            for (int i = 0; i < count; ++i) {
                const uint8_t note = (uint8_t)notes[i];
                emit(out, n, max, frame, (uint8_t)(0x90 | ch), note, (uint8_t)vel);
                active[tr][note] = (uint8_t)(ch + 1);
                schedule(t + 0.45 * kStep, (uint8_t)(0x80 | ch), note, 0, tr);
                schedule(t + 0.5 * kStep, (uint8_t)(0x90 | ch), note, (uint8_t)std::min(127, vel + 12), tr);
                schedule(t + 0.95 * kStep, (uint8_t)(0x80 | ch), note, 0, tr);
            }
            channelUsed[ch] = true;
            tieHold[tr] = false;
            continue;
        }
        const bool legato = tieHold[tr];
        if (!legato) // neuer Anschlag: alles der Spur zuerst beenden
            for (int k = 0; k < 128; ++k)
                noteOff(tr, k, out, n, max, frame);
        for (int i = 0; i < count; ++i) {
            const uint8_t note = (uint8_t)notes[i];
            if (legato && active[tr][note] == ch + 1) { // gebunden, gleiche Tonhöhe: nur das Ende verschieben
                for (Pending& p : pending)
                    if (p.active && p.track == tr && p.d1 == note && (p.status & 0xf0) == 0x80)
                        p.beat = t + len;
                continue;
            }
            noteOff(tr, note, out, n, max, frame);
            emit(out, n, max, frame, (uint8_t)(0x90 | ch), note, (uint8_t)vel);
            active[tr][note] = (uint8_t)(ch + 1);
            schedule(t + len, (uint8_t)(0x80 | ch), note, 0, tr);
        }
        if (legato) // gebunden, andere Tonhöhe: alte Noten erst nach dem neuen Anschlag beenden (Glide)
            for (int k = 0; k < 128; ++k)
                if (active[tr][k] && !inStep(k))
                    noteOff(tr, k, out, n, max, frame);
        channelUsed[ch] = true;
        tieHold[tr] = step.tie;
    }
}

int Sequencer::process(uint32_t frames, MidiOut* out, int max)
{
    int n = 0;
    if (frames == 0)
        return 0;
    const bool playOn = params[kPlay] >= 0.5f;
    const bool nowRunning = hostPlaying || playOn;
    if (!nowRunning || (hostPlaying != wasHost && running) || (playOn && !playWasOn && !hostPlaying)) {
        if (running)
            stopAll(out, n, max, 0);
        running = false;
    }
    playWasOn = playOn;
    params[kPlaying] = nowRunning ? 1.0f : 0.0f;
    if (!nowRunning) {
        for (int t = 0; t < kTracks; ++t)
            params[kStep1 + t] = -1.0f;
        playingPattern = (int)params[kPattern]; // im Stillstand gilt die Auswahl sofort
        params[kPlayingPattern] = params[kQueuedPattern] = params[kPattern];
        return n;
    }

    const double bpm = hostPlaying ? hostTempo : (double)params[kBpm];
    const double bpf = bpm / 60.0 / sr; // Schläge pro Sample
    double b0 = hostPlaying ? hostBeat : internalBeat;
    if (!running) { // Start
        if (!hostPlaying)
            b0 = internalBeat = 0.0;
        expectedBeat = b0;
    }
    if (running && std::fabs(b0 - expectedBeat) > 1e-3) { // Sprung (Loop, Spulen): Noten aus, neu ausrichten
        stopAll(out, n, max, 0);
        running = false;
    }
    if (!running) {
        running = true;
        wasHost = hostPlaying;
        const int64_t g0 = (int64_t)std::ceil(b0 / kStep - 1e-9);
        lastFired = g0 - 1;
        patternStart = (int64_t)std::floor((double)g0 / kBarSteps) * kBarSteps;
        playingPattern = (int)params[kPattern]; // Start: gewähltes Pattern sofort
    }
    const double b1 = b0 + frames * bpf;
    // fällig, wenn der gerundete Sample-Index in diesen Block fällt (nicht t < b1: Rundung an der Blockgrenze)
    auto due = [&](double t) { return std::floor((t - b0) / bpf + 0.5) < (double)frames; };

    // Ereignisse in Zeitreihenfolge: geplante (Note-Offs, Rollen) vor Schritten zur selben Zeit
    for (;;) {
        int pi = -1;
        for (int i = 0; i < kMaxPending; ++i)
            if (pending[i].active && due(pending[i].beat) && (pi < 0 || pending[i].beat < pending[pi].beat))
                pi = i;
        const int64_t g = lastFired + 1;
        const double tg = stepTime(g);
        const bool stepDue = due(tg);
        if (pi < 0 && !stepDue)
            break;
        if (pi >= 0 && (!stepDue || pending[pi].beat <= tg)) {
            Pending& p = pending[pi];
            const uint32_t frame = std::min<uint32_t>(frames - 1, (uint32_t)std::max(0.0, std::floor((p.beat - b0) / bpf + 0.5)));
            emit(out, n, max, frame, p.status, p.d1, p.d2);
            if ((p.status & 0xf0) == 0x80 && p.track >= 0)
                active[p.track][p.d1] = 0;
            else if ((p.status & 0xf0) == 0x90 && p.track >= 0)
                active[p.track][p.d1] = (uint8_t)((p.status & 15) + 1);
            p.active = false;
        } else {
            fireStep(g, tg, b0, bpf, frames, out, n, max);
            lastFired = g;
        }
    }
    expectedBeat = b1;
    if (!hostPlaying)
        internalBeat = b1;
    params[kPlayingPattern] = (float)playingPattern;
    params[kQueuedPattern] = params[kPattern];
    return n;
}

} // namespace offbeat
