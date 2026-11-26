// DPF-Hülle von Offbeat Gearbox: Host-Takt und Parameter an den Sequencer, MIDI-Ereignisse an den Host
// (Eingang wird durchgereicht), Raster im State „set“ (JSON), Fernbedienung über Core/Server.
#include "Core.hpp"
#include "DistrhoPlugin.hpp"
#include "OffbeatParam.hpp"
#include "Sequencer.hpp"
#include "SharedClock.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

using namespace offbeat;

class GearboxPlugin : public Plugin {
public:
    GearboxPlugin()
        : Plugin(kParamCount, 0, 1)
    {
        seq.setSampleRate((float)getSampleRate());
        seq.setSet(core.acquire());
    }

protected:
    const char* getLabel() const override { return "OffbeatGearbox"; }
    const char* getDescription() const override { return "8-Spur-Sequencer (MIDI) mit iPad-Fernbedienung"; }
    const char* getMaker() const override { return "Offbeat"; }
    const char* getHomePage() const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }

    void initParameter(uint32_t index, Parameter& parameter) override { fillParameter(kParams[index], parameter); }

    void initState(uint32_t, State& state) override
    {
        state.key = "set";
        state.label = "Raster";
        Set s;
        defaultSet(s);
        state.defaultValue = setToJson(s).c_str();
        state.hints = kStateIsHostReadable;
    }

    String getState(const char* key) const override
    {
        return std::strcmp(key, "set") == 0 ? String(core.stateJson().c_str()) : String();
    }

    void setState(const char* key, const char* value) override
    {
        std::string err;
        if (std::strcmp(key, "set") == 0)
            core.setStateJson(value, err); // fehlerhaftes JSON: Raster bleibt
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case kRemotePort: return (float)core.server.port();
        case kRemotePin: return (float)core.server.pin();
        case kRemoteClients: return (float)core.server.clients();
        case kRevision: return (float)(core.revision() % 10000000u);
        default: return seq.getParam((int)index);
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        seq.setParam((int)index, value);
        if (index == kRemote)
            core.server.setEnabled(value >= 0.5f);
    }

    void sampleRateChanged(double newSampleRate) override { seq.setSampleRate((float)newSampleRate); }

    void activate() override { panicPending = true; } // nach Pause/Neustart: hängende Noten der Empfänger löschen

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* events, uint32_t eventCount) override
    {
        for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; ++c) // nur Standalone: stumm
            std::memset(outputs[c], 0, sizeof(float) * frames);
        int id;
        float value;
        while (core.popParam(id, value)) { // vom iPad: im Host setzen, sonst nur intern
            seq.setParam(id, value);
            requestParameterValueChange((uint32_t)id, value);
            if (id == kRemote)
                core.server.setEnabled(value >= 0.5f);
        }
        seq.setSet(core.acquire());

        const TimePosition& tp = getTimePosition();
        double hostBpm = 0.0;
        if (tp.bbt.valid) {
            const double beat = (double)(tp.bbt.bar - 1) * tp.bbt.beatsPerBar + (tp.bbt.beat - 1) + tp.bbt.tick / tp.bbt.ticksPerBeat;
            hostBpm = tp.bbt.beatsPerMinute;
            seq.setTransport(tp.playing, beat, hostBpm);
        } else {
            seq.setTransport(false, 0.0, 0.0);
        }
        double clockBeat = 0.0, clockBpm = 0.0;
        const bool clockOk = clock.read(clockBeat, clockBpm);
        seq.setClock(clockOk, clockBeat, clockBpm);

        int n = 0;
        if (panicPending) {
            n = seq.panic(out, kMaxOut);
            panicPending = false;
        }
        n += seq.process(frames, out + n, kMaxOut - n);

        // eigene Ereignisse (sortiert) mit dem durchgereichten Eingang zusammenführen
        uint32_t e = 0;
        for (int i = 0; i <= n; ++i) {
            const uint32_t until = i < n ? out[i].frame : frames;
            for (; e < eventCount && events[e].frame <= until && (i < n || events[e].frame < frames); ++e)
                writeMidiEvent(events[e]);
            if (i < n) {
                MidiEvent ev;
                ev.frame = out[i].frame;
                ev.size = (out[i].data[0] & 0xf0) == 0xc0 ? 2 : 3;
                std::memcpy(ev.data, out[i].data, 3);
                ev.dataExt = nullptr;
                writeMidiEvent(ev);
            }
        }

        float p[kParamCount];
        for (int i = 0; i < kParamCount; ++i)
            p[i] = seq.getParam(i);
        core.publishLive(p, tp.playing && hostBpm > 0.0 ? hostBpm : (double)p[kBpm]);
    }

private:
    static constexpr int kMaxOut = 512;
    mutable Core core;
    Sequencer seq;
    SharedClock clock; // Takt von Offbeat Clutch
    MidiOut out[kMaxOut];
    bool panicPending = true;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GearboxPlugin)
};

Plugin* createPlugin() { return new GearboxPlugin(); }

END_NAMESPACE_DISTRHO
