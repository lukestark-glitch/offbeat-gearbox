// Kern von Offbeat Gearbox zwischen Host, Audio-Thread und Fernbedienung:
// - Schatten-Raster (Mutex) für State, Oberfläche und Server; jede Änderung erzeugt eine neue Kopie,
//   die der Audio-Thread lock-frei per Zeigertausch übernimmt (acquire). Alte Kopien gibt der
//   Server-Thread frei (remoteIdle), nie der Audio-Thread.
// - Parameter-Befehle vom iPad (mute/pattern/play/…) gehen über eine lock-freie Warteschlange zum DSP.
// - Live-Zustand (Cursor, Pattern, Play) schreibt der Audio-Thread in Atomics, der Server liest sie.
#pragma once

#include "Model.hpp"
#include "Params.hpp"
#include "Server.hpp"

#include <atomic>
#include <mutex>

namespace offbeat {

class Core : public RemoteHost {
public:
    Core();
    ~Core() override;

    // Nicht im Audio-Thread
    std::string stateJson();
    bool setStateJson(const char* json, std::string& err);  // ganzes Set (Host lädt Projekt, EINFÜGEN)
    bool command(const std::string& lines, std::string& err, bool allowParams);
    uint32_t revision() const { return rev; }

    // Audio-Thread: zu Blockbeginn
    const Set* acquire();
    bool popParam(int& id, float& value);
    void publishLive(const float* seqParams, double bpm); // Anzeige-Werte des Sequencers

    // RemoteHost (Server-Thread)
    std::string remoteState() override { return stateJson(); }
    std::string remoteLive() override;
    uint32_t remoteRevision() override { return rev; }
    bool remoteCommand(const std::string& lines, std::string& err) override { return command(lines, err, true); }
    void remoteIdle() override { collect(); }

private:
    void publish(); // mit gehaltenem Mutex
    void collect();

    std::mutex mutex;
    Set shadow;
    std::atomic<uint32_t> rev { 1 };
    std::atomic<Set*> pending { nullptr }, garbage { nullptr };
    Set* current = nullptr; // gehört dem Audio-Thread

    struct ParamMsg {
        int id;
        float value;
    };
    static constexpr int kQueue = 64;
    ParamMsg queue[kQueue];
    std::atomic<int> head { 0 }, tail { 0 }; // ein Erzeuger (Server-Thread), ein Verbraucher (Audio-Thread)

    std::atomic<bool> livePlaying { false }, liveFill { false };
    std::atomic<int> livePattern { 0 }, liveQueued { 0 }, liveSteps[kTracks], liveMutes { 0 };
    std::atomic<float> liveBpm { 160.0f }, liveSwing { 0.0f };

public:
    Server server; // als Letztes erzeugt, als Erstes zerstört: sein Thread sieht nur fertige Member
};

} // namespace offbeat
