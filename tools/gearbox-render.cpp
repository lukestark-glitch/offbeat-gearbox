// gearbox-render: Abnahme von Offbeat Gearbox ohne Host. Simuliert den Host-Transport und prüft die
// MIDI-Ereignisse; startet für tools/remote-test.sh den Fernbedienungs-Server ohne Plugin.
//
//   gearbox-render --check-all                                  (wie make check)
//   gearbox-render --serve [--port 8080] [--pin 1234] [--fake-peer 8.8.8.8]
//   gearbox-render --dump [--bpm 160] [--beats 8]               (Ereignisse als Text)
#include "Model.hpp"
#include "Sequencer.hpp"
#ifdef GEARBOX_SERVER
#include "Server.hpp"
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace offbeat;

namespace {

int failed = 0;
void expect(const std::string& what, bool ok)
{
    std::printf("  [%s] %s\n", ok ? "OK " : "FEHLER", what.c_str());
    failed += ok ? 0 : 1;
}

struct Ev {
    int64_t sample;
    uint8_t s, d1, d2;
};

// Host mit festem Tempo und Blockgröße; hook(pos) darf vor jedem Block Parameter/Raster ändern
struct Sim {
    Sequencer seq;
    Set set;
    int sr = 48000;
    uint32_t block = 512;
    std::vector<Ev> events;
    int64_t base = 0; // Sampleposition über mehrere run()-Aufrufe

    Sim() { defaultSet(set); seq.setSampleRate((float)sr); seq.setSet(&set); }

    void cmd(const char* line)
    {
        int id;
        float v;
        std::string err;
        if (applyCommand(set, line, id, v, err) == CommandResult::Param)
            seq.setParam(id, v);
    }

    template <class Hook>
    void run(double bpm, double seconds, bool host, Hook hook, double startBeat = 0.0)
    {
        MidiOut out[512];
        const int64_t total = (int64_t)(seconds * sr);
        double offset = startBeat; // Host-Takt wie echte Hosts aus der Sampleposition (Sprünge verschieben offset)
        for (int64_t pos = 0; pos < total; pos += block) {
            const uint32_t n0 = (uint32_t)std::min<int64_t>(block, total - pos);
            double beat = offset + (double)pos / sr * bpm / 60.0;
            const double before = beat;
            hook(pos, beat);
            offset += beat - before;
            seq.setTransport(host, beat, bpm);
            const int n = seq.process(n0, out, 512);
            for (int i = 0; i < n; ++i)
                events.push_back({ base + pos + out[i].frame, out[i].data[0], out[i].data[1], out[i].data[2] });
        }
        base += total;
    }
    void run(double bpm, double seconds, bool host = true)
    {
        run(bpm, seconds, host, [](int64_t, double&) {});
    }
};

// Note-Ons eines Kanals (1–16)
std::vector<Ev> onsOf(const std::vector<Ev>& ev, int channel)
{
    std::vector<Ev> out;
    for (const Ev& e : ev)
        if ((e.s & 0xf0) == 0x90 && e.d2 > 0 && (e.s & 15) == channel - 1)
            out.push_back(e);
    return out;
}

// Zählt klingende Noten nach; false, wenn ein Note-Off ohne Note-On kommt oder am Ende etwas hängt
bool noHanging(const std::vector<Ev>& ev, int& hanging)
{
    std::map<int, int> on;
    for (const Ev& e : ev) {
        const int key = (e.s & 15) * 128 + e.d1;
        if ((e.s & 0xf0) == 0x90 && e.d2 > 0)
            ++on[key];
        else if ((e.s & 0xf0) == 0x80 || (e.s & 0xf0) == 0x90)
            on[key] = std::max(0, on[key] - 1);
        else if ((e.s & 0xf0) == 0xb0 && e.d1 == 123)
            for (auto& kv : on)
                if (kv.first / 128 == (e.s & 15))
                    kv.second = 0;
    }
    hanging = 0;
    for (auto& kv : on)
        hanging += kv.second;
    return hanging == 0;
}

int64_t sampleOf(double beat, double bpm, int sr) { return (int64_t)std::floor(beat * 60.0 / bpm * sr + 0.5); }

int checkAll()
{
    char buf[200];
    std::puts("== Timing sampelgenau (Kick auf jedem Schlag, Host-Takt, Blöcke 512)");
    for (double bpm : { 150.0, 160.0, 165.0 }) {
        Sim sim;
        sim.run(bpm, 8.0);
        const std::vector<Ev> kicks = onsOf(sim.events, 1);
        int64_t worst = 0;
        for (size_t k = 0; k < kicks.size(); ++k)
            worst = std::max<int64_t>(worst, std::llabs(kicks[k].sample - sampleOf((double)k, bpm, sim.sr)));
        const size_t want = (size_t)std::ceil(8.0 * bpm / 60.0 - 1e-9);
        std::snprintf(buf, sizeof(buf), "%g BPM: %zu Kicks (erwartet %zu), größte Abweichung %lld Samples", bpm, kicks.size(), want, (long long)worst);
        expect(buf, kicks.size() == want && worst == 0);
    }

    std::puts("== Swing 50 %: ungerade 16tel um 1/3 Schritt später");
    {
        Sim sim;
        sim.seq.setParam(kSwing, 50);
        for (int s = 0; s < 16; ++s) {
            char c[40];
            std::snprintf(c, sizeof(c), "step 0 2 %d 2", s);
            sim.cmd(c);
        }
        sim.run(160, 4.0);
        const std::vector<Ev> hats = onsOf(sim.events, 3);
        int64_t worst = 0;
        for (size_t g = 0; g < hats.size(); ++g) {
            const double t = g * 0.25 + ((g & 1) ? 0.25 / 3.0 : 0.0);
            worst = std::max<int64_t>(worst, std::llabs(hats[g].sample - sampleOf(t, 160, sim.sr)));
        }
        std::snprintf(buf, sizeof(buf), "%zu Hats, größte Abweichung vom Swing-Raster %lld Samples", hats.size(), (long long)worst);
        expect(buf, hats.size() >= 40 && worst == 0);
    }

    std::puts("== Pattern-Wechsel genau am Taktanfang");
    {
        Sim sim;
        sim.cmd("clear 1");
        sim.cmd("step 1 0 2 3"); // Pattern B: Kick nur auf Schritt 3
        int64_t requestAt = 0;
        sim.run(160, 6.0, true, [&](int64_t pos, double& beat) {
            if (beat >= 5.3 && !requestAt) { // mitten in Takt 2
                sim.cmd("pattern 1");
                requestAt = pos;
            }
        });
        const std::vector<Ev> kicks = onsOf(sim.events, 1);
        bool aUntilBar = true, bAfter = false;
        for (const Ev& e : kicks) {
            if (e.sample < sampleOf(8.0, 160, sim.sr))
                aUntilBar &= std::llabs(e.sample - sampleOf(std::round(e.sample * 160.0 / 60 / sim.sr), 160, sim.sr)) == 0;
            else
                bAfter |= e.sample == sampleOf(8.5, 160, sim.sr); // Takt 3, Schritt 3
        }
        int afterBar = 0;
        for (const Ev& e : kicks)
            afterBar += e.sample >= sampleOf(8.0, 160, sim.sr) && e.sample < sampleOf(12.0, 160, sim.sr);
        expect("Pattern A spielt bis Taktende weiter (Kick auf jedem Schlag)", aUntilBar && kicks.size() > 8);
        expect("Pattern B ab Taktanfang (Beat 8): genau ein Kick pro Takt auf Schritt 3", bAfter && afterBar == 1);
    }

    std::puts("== Polymetrik: Spur mit 3 Schritten gegen 16");
    {
        Sim sim;
        sim.cmd("clear 0");
        sim.cmd("length 0 0 3");
        sim.cmd("step 0 0 0 2");
        sim.cmd("step 0 1 0 2"); // Bass: Schritt 1 von 16
        sim.run(160, 3.0);
        const std::vector<Ev> a = onsOf(sim.events, 1), b = onsOf(sim.events, 2);
        bool ok = a.size() >= 10;
        for (size_t k = 0; k < a.size(); ++k)
            ok &= a[k].sample == sampleOf(k * 0.75, 160, sim.sr);
        for (size_t k = 0; k < b.size(); ++k)
            ok &= b[k].sample == sampleOf(k * 4.0, 160, sim.sr);
        std::snprintf(buf, sizeof(buf), "3er-Spur alle 3 Schritte (%zu Anschläge), 16er-Spur jeden Takt (%zu)", a.size(), b.size());
        expect(buf, ok);
    }

    std::puts("== Tie: gebundene Schritte ohne neuen Anschlag");
    {
        Sim sim;
        sim.cmd("clear 0");
        sim.cmd("step 0 1 0 2");
        sim.cmd("step 0 1 1 2");
        sim.cmd("tie 0 1 0 1");
        sim.run(160, 1.4);
        const std::vector<Ev> b = onsOf(sim.events, 2);
        int64_t off = -1;
        for (const Ev& e : sim.events)
            if ((e.s & 0xf0) == 0x80 && (e.s & 15) == 1 && off < 0)
                off = e.sample;
        expect("Schritt 1+2 gebunden: ein Anschlag pro Takt, Ende nach Schritt 2 + Gate", b.size() == 1 && off == sampleOf(0.25 + 0.5 * 0.25, 160, sim.sr));
    }

    std::puts("== Keine hängenden Noten (Stop, Pattern-Wechsel, Mutes, Fill, Loop-Sprung, Live-Änderungen)");
    {
        Sim sim;
        sim.cmd("copy 0 1");
        sim.cmd("tie 1 1 2 1");
        sim.cmd("step 1 1 3 3");
        sim.cmd("length 1 2 5");
        sim.cmd("gate 4 0.95");
        uint32_t rnd = 12345;
        auto next = [&] { rnd = rnd * 1664525u + 1013904223u; return rnd >> 8; };
        bool host = true;
        sim.run(165, 30.0, true, [&](int64_t pos, double& beat) {
            if (pos % (512 * 37) == 0) {
                char c[48];
                switch (next() % 7) {
                case 0: std::snprintf(c, sizeof(c), "pattern %u", next() % 2); break;
                case 1: std::snprintf(c, sizeof(c), "mute %u %u", next() % 8, next() % 2); break;
                case 2: std::snprintf(c, sizeof(c), "fill %u", next() % 2); break;
                case 3: std::snprintf(c, sizeof(c), "step %u %u %u %u", next() % 2, next() % 8, next() % 16, next() % 4); break;
                case 4: std::snprintf(c, sizeof(c), "tie %u %u %u 1", next() % 2, next() % 8, next() % 16); break;
                case 5: beat = std::floor(beat / 4) * 4 - 4; std::snprintf(c, sizeof(c), "swing %u", next() % 76); break; // Loop zurück
                default: std::snprintf(c, sizeof(c), "length %u %u %u", next() % 2, next() % 8, 1 + next() % 64);
                }
                sim.cmd(c);
            }
            (void)host;
        });
        sim.run(165, 0.1, false); // Host stoppt
        int hanging;
        const bool ok = noHanging(sim.events, hanging);
        std::snprintf(buf, sizeof(buf), "%zu Ereignisse, nach Stop hängen %d Noten", sim.events.size(), hanging);
        expect(buf, ok && sim.events.size() > 300);
    }

    std::puts("== Interner Play/Stop mit eigenem BPM");
    {
        Sim sim;
        sim.seq.setParam(kBpm, 150);
        sim.seq.setParam(kPlay, 1);
        sim.run(0, 4.0, false);
        sim.seq.setParam(kPlay, 0);
        sim.run(0, 0.1, false);
        const std::vector<Ev> kicks = onsOf(sim.events, 1);
        int hanging;
        bool ok = noHanging(sim.events, hanging) && kicks.size() == 10;
        for (size_t k = 0; k < kicks.size(); ++k)
            ok &= kicks[k].sample == sampleOf((double)k, 150, sim.sr);
        std::snprintf(buf, sizeof(buf), "%zu Kicks bei 150 BPM in 4 s, nach Stop hängen %d Noten", kicks.size(), hanging);
        expect(buf, ok);
    }

    std::puts("== JSON: Set und Pattern hin und zurück");
    {
        Set a, b;
        defaultSet(a);
        std::snprintf(a.tracks[2].name, sizeof(a.tracks[2].name), "Hat \"zu\"");
        a.patterns[3].tracks[1].length = 7;
        a.patterns[3].tracks[1].steps[5] = { kVelGhost, true };
        std::string err;
        const bool setOk = applyJsonText(b, setToJson(a).c_str(), 0, err) && setToJson(a) == setToJson(b);
        Set c;
        defaultSet(c);
        const bool patOk = applyJsonText(c, patternToJson(a, 3).c_str(), 6, err) && patternToJson(c, 6) == patternToJson(a, 3);
        Set d = a;
        const bool badRejected = !applyJsonText(d, "{\"pattern\":{\"tracks\":[{\"steps\":\"x?\"}]}}", 0, err) && setToJson(d) == setToJson(a);
        expect("Set-JSON hin und zurück identisch", setOk);
        expect("Pattern-JSON in anderes Pattern eingefügt", patOk);
        expect("fehlerhaftes JSON abgelehnt, Raster unverändert (" + err + ")", badRejected);
    }

    std::printf("%s\n", failed ? "NICHT BESTANDEN" : "BESTANDEN");
    return failed ? 1 : 0;
}

void dump(double bpm, double beats)
{
    Sim sim;
    sim.run(bpm, beats * 60.0 / bpm);
    for (const Ev& e : sim.events)
        std::printf("%8lld  %02x %3d %3d\n", (long long)e.sample, e.s, e.d1, e.d2);
}

} // namespace

#ifdef GEARBOX_SERVER
int serve(int argc, char** argv);
#endif

int main(int argc, char** argv)
{
    if (argc >= 2 && !std::strcmp(argv[1], "--check-all"))
        return checkAll();
    if (argc >= 2 && !std::strcmp(argv[1], "--dump")) {
        double bpm = 160, beats = 8;
        for (int i = 2; i + 1 < argc; i += 2) {
            if (!std::strcmp(argv[i], "--bpm")) bpm = std::atof(argv[i + 1]);
            if (!std::strcmp(argv[i], "--beats")) beats = std::atof(argv[i + 1]);
        }
        dump(bpm, beats);
        return 0;
    }
#ifdef GEARBOX_SERVER
    if (argc >= 2 && !std::strcmp(argv[1], "--serve"))
        return serve(argc, argv);
#endif
    std::puts("gearbox-render --check-all | --dump [--bpm 160] [--beats 8] | --serve [--port 8080] [--pin 1234] [--fake-peer IP]");
    return 2;
}

#ifdef GEARBOX_SERVER
#include "Core.hpp"

#include <csignal>
#include <thread>

namespace {
std::atomic<bool> stopServe { false };
}

// Server ohne Plugin für tools/remote-test.sh: Core mit Raster, ein Sequencer läuft intern mit (Live-Daten)
int serve(int argc, char** argv)
{
    int port = 8080, pin = 0;
    uint32_t fake = 0;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--port")) port = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--pin")) pin = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--fake-peer")) {
            unsigned a, b, c, d;
            if (std::sscanf(argv[i + 1], "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
                fake = a << 24 | b << 16 | c << 8 | d;
        }
    }
    std::signal(SIGINT, [](int) { stopServe = true; });
    std::signal(SIGTERM, [](int) { stopServe = true; });
    Core core;
    core.server.setFirstPort(port);
    if (pin)
        core.server.setFixedPin(pin);
    if (fake)
        core.server.setFakePeer(fake);
    core.server.setEnabled(true);
    Sequencer seq;
    seq.setSampleRate(48000);
    MidiOut out[512];
    bool announced = false;
    while (!stopServe) { // Audio-Thread nachspielen: 512er-Blöcke in Echtzeit
        int id;
        float v;
        while (core.popParam(id, v))
            seq.setParam(id, v);
        seq.setSet(core.acquire());
        seq.setTransport(false, 0, 0);
        seq.process(512, out, 512);
        float p[kParamCount];
        for (int i = 0; i < kParamCount; ++i)
            p[i] = seq.getParam(i);
        core.publishLive(p, seq.getParam(kBpm));
        if (!announced && core.server.port()) {
            std::printf("port %d pin %04d\n", core.server.port(), core.server.pin());
            std::fflush(stdout);
            announced = true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(10667));
    }
    return 0;
}
#endif
