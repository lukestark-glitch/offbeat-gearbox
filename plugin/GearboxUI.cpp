// Oberfläche von Offbeat Gearbox: Transport (Play, Patterns A–H, Fill, Swing, BPM), Raster 8 Spuren × 16
// Schritte je Seite (bis 64), je Spur Name, Mute/Solo, Kanal, Note, Gate, Lautstärke, Länge, Rotieren;
// unten Remote (iPad) und Kopieren/Einfügen von Pattern und Set als JSON. Look: OffbeatKit.hpp.
//
// Bedienung im Raster: Klick setzt/löscht einen normalen Schritt; mit STÄRKE an schaltet er
// aus → normal → laut → Ghost → aus. Rechtsklick bindet (Tie). Klick auf den Namen einer Melodie-Spur
// öffnet ihre Piano-Roll (Tonhöhen × Schritte; Klick setzt/löscht eine Note, mehrere = Akkord, die Note
// erklingt über den MIDI-Ausgang). Kanal/Note/Gate/Lautstärke: Klick +, Rechtsklick −, Mausrad.
// Name: Rechtsklick, tippen, Enter.
// Raster-Änderungen gehen als ganzes Set per setState("set") an das Plugin; Änderungen vom iPad holt die
// Oberfläche über localhost (GET /state), sobald der Revisionszähler sich ändert.
#include "GearboxImage.h" // aus assets/gearbox.png erzeugt (xxd -i), siehe plugin/Makefile
#include "Model.hpp"
#include "OffbeatKit.hpp"
#include "Server.hpp"
#include "Songs.hpp"

START_NAMESPACE_DISTRHO

using namespace offbeat;
using kit::kLabelH;

namespace {
constexpr float kW = 1220, kH = 720, kMargin = 20;
constexpr float kTransportY = 104, kGridY = 186, kRowH = 46, kRemoteY = 618;
constexpr float kSoundW = 96, kNameW = 84, kSmallW = 30, kFieldW = 46, kCellW = 30, kCellGap = 3;

enum Button {
    kBtnStep = 1000,  // + Spur·64 + Schritt
    kBtnMute = 2000, kBtnSolo = 2100, kBtnName = 2200, kBtnLenDown = 2300, kBtnLenUp = 2400,
    kBtnRotL = 2500, kBtnRotR = 2600, kBtnPattern = 2700, kBtnPage = 2750,
    kBtnPlay = 2800, kBtnFill = 2801, kBtnRemote = 2802, kBtnCopyPattern = 2803, kBtnCopySet = 2804,
    kBtnPasteGrid = 2805, kBtnClear = 2806, kBtnVelMode = 2807,
    kBtnChannel = 3000, kBtnNote = 3100, kBtnGate = 3200, kBtnVolume = 3300, kBtnSound = 3400, // + Spur
    kBtnOctDown = 4100, kBtnOctUp = 4101, kBtnRollBack = 4102, kBtnRollMute = 4103, kBtnRollSolo = 4104,
    kBtnSongs = 4200, kBtnSongSave = 4201, kBtnChainOn = 4202, kBtnChainClear = 4203,
    kBtnSongLoad = 4300,  // + Index in der Song-Liste
    kBtnChainAdd = 4400,  // + Pattern
    kBtnChainDown = 4500, kBtnChainUp = 4600, kBtnChainDel = 4700, // + Eintrag
    kBtnRoll = 5000, // + (Versatz − kKeyLow) · 64 + Schritt (Zelle der Piano-Roll)
};

std::string noteName(int note)
{
    static const char* const kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string(kNames[note % 12]) + std::to_string(note / 12 - 1);
}

kit::Theme gearboxTheme()
{
    kit::Theme t;
    t.title = "OFFBEAT";
    t.subtitle = "GEARBOX";
    t.brand = Color(255, 163, 26);
    t.brandInk = Color(110, 50, 0);
    t.night = Color(13, 8, 4);
    t.night2 = Color(62, 28, 4);
    t.light = Color(255, 205, 130);
    t.plate = Color(26, 15, 6);
    t.track = Color(86, 50, 14);
    t.text = Color(255, 241, 224);
    t.dim = Color(225, 185, 135);
    return t;
}

} // namespace

class GearboxUI : public OffbeatKitUI {
public:
    GearboxUI()
        : OffbeatKitUI(kW, kH, gearboxTheme())
    {
        gearbox = createImageFromMemory(gearbox_png, gearbox_png_len, IMAGE_GENERATE_MIPMAPS);
        defaultSet(model);
    }

protected:
    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, "set") != 0)
            return;
        std::string err;
        applyJsonText(model, value, 0, err);
        redraw();
    }

    void uiIdle() override
    {
        // Änderungen vom iPad: Zustand über localhost holen (nur wenn die Fernbedienung läuft)
        const uint32_t rev = (uint32_t)values[kRevision];
        const int port = (int)values[kRemotePort];
        if (port > 0 && rev != fetchedRevision) {
            fetchedRevision = rev;
            fetchState();
        }
        OffbeatKitUI::uiIdle();
    }

    void drawContent() override
    {
        drawLogo(145, 50);
        drawImage(gearbox, kW - kMargin - 330, 4, 330, 330 * 768.0f / 2048.0f);

        // Transport
        drawPanel(kMargin, kTransportY, kW - 2 * kMargin, 72, "TRANSPORT", th.brand);
        const float ty = kTransportY + kLabelH + 10;
        const bool playing = values[kPlaying] >= 0.5f;
        pill(kBtnPlay, kMargin + 14, ty, 96, 30, playing ? "Stop" : "Play", th.brand, playing);
        const int running = (int)values[kPlayingPattern], selected = editPattern();
        for (int p = 0; p < kPatterns; ++p) {
            const char name[2] = { (char)('A' + p), 0 };
            const float x = kMargin + 130 + p * 42;
            pill(kBtnPattern + p, x, ty, 36, 30, name, th.brand, p == selected);
            if (p == running && selected != running && playing) { // läuft noch bis Taktende, dann kommt die Auswahl
                beginPath();
                roundedRect(x - 3, ty - 3, 42, 36, 18);
                strokeColor(th.light);
                strokeWidth(2);
                stroke();
            }
        }
        pill(kBtnFill, kMargin + 474, ty, 70, 30, "Fill", th.brand, values[kFill] >= 0.5f);
        pill(kBtnVelMode, kMargin + 552, ty, 90, 30, "ST\xc3\x84RKE", th.light, velMode);
        compact(kSwing, kMargin + 656, ty - 6, 110, th.brand);
        compact(kBpm, kMargin + 776, ty - 6, 110, th.brand);
        for (int k = 0; k < 4; ++k) {
            char label[4];
            std::snprintf(label, sizeof(label), "%d", k + 1);
            const bool enabled = k * 16 < maxLength();
            pill(kBtnPage + k, kMargin + 904 + k * 40, ty, 34, 30, label, enabled ? th.brand : th.track, k == page);
        }
        pill(kBtnSongs, kMargin + 1074, ty, 92, 30, model.chainOn ? "SONG \xe2\x96\xb8" : "SONG", th.light, songView);
        if (songView)
            drawSongs();
        else if (rollTrack >= 0)
            drawRoll();
        else
            drawGrid();
        drawRemote();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        const float s = getWidth() / uiW;
        const float x = ev.pos.getX() / s, y = ev.pos.getY() / s;
        if (!ev.press && fillHeld && ev.button == kMouseButtonLeft) { // Fill nur solange gedrückt
            fillHeld = false;
            setValue(kFill, 0, true);
        }
        if (ev.press && editName >= 0) {
            editName = -1;
            sendSet();
        }
        if (!ev.press && previewNote >= 0) { // Note der Piano-Roll losgelassen
            sendNote((uint8_t)previewChannel, (uint8_t)previewNote, 0);
            previewNote = -1;
        }
        const Hit* h = hitAt(x, y);
        if (ev.press && ev.button == kMouseButtonRight && h && h->kind == kButton) {
            const int b = h->button;
            if (b >= kBtnName && b < kBtnName + kTracks) { // umbenennen
                editName = b - kBtnName;
                nameText = model.tracks[editName].name;
                redraw();
                return true;
            }
            if (b >= kBtnRoll && b < kBtnRoll + kKeyCount * kMaxSteps) { // Tie in der Piano-Roll
                Step& st = model.patterns[editPattern()].tracks[rollTrack].steps[(b - kBtnRoll) % kMaxSteps];
                if (st.vel) {
                    st.tie = !st.tie;
                    sendSet();
                }
                return true;
            }
            if (b >= kBtnStep && b < kBtnStep + kTracks * kMaxSteps) {
                Step& st = model.patterns[editPattern()].tracks[(b - kBtnStep) / kMaxSteps].steps[(b - kBtnStep) % kMaxSteps];
                st.tie = !st.tie;
                sendSet();
                return true;
            }
            if (b >= kBtnChannel && b < kBtnSound + kTracks) {
                adjustField(b, -1);
                return true;
            }
        }
        if (ev.press && ev.button == kMouseButtonLeft && h && h->kind == kButton && h->button >= kBtnName && h->button < kBtnName + kTracks) {
            const int t = h->button - kBtnName; // Melodie-Spur: Piano-Roll öffnen
            if (model.tracks[t].melody) {
                rollTrack = t;
                rollView = std::max(kKeyLow, std::min(0, -(model.tracks[t].note % 12))); // ab dem C unter der Spur-Note
                redraw();
            }
            return true;
        }
        return OffbeatKitUI::onMouse(ev);
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float s = getWidth() / uiW;
        const Hit* h = hitAt(ev.pos.getX() / s, ev.pos.getY() / s);
        if (h && h->kind == kButton && h->button >= kBtnChannel && h->button < kBtnSound + kTracks) {
            adjustField(h->button, ev.delta.getY() > 0 ? 1 : -1);
            return true;
        }
        return OffbeatKitUI::onScroll(ev);
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (songView && editName < 0 && ev.press) { // Song-Ansicht: Tippen schreibt den Namen, Enter speichert
            if (ev.key == kKeyEnter)
                onButton(kBtnSongSave);
            else if (ev.key == kKeyEscape)
                songView = false, redraw();
            else if (ev.key == kKeyBackspace && !songText.empty()) {
                do
                    songText.pop_back();
                while (!songText.empty() && ((unsigned char)songText.back() & 0xc0) == 0x80);
                redraw();
            }
            return true;
        }
        if (editName < 0)
            return OffbeatKitUI::onKeyboard(ev);
        if (!ev.press)
            return true;
        if (ev.key == kKeyEnter || ev.key == kKeyEscape) {
            if (ev.key == kKeyEnter)
                std::snprintf(model.tracks[editName].name, sizeof(TrackConf::name), "%s", nameText.c_str());
            editName = -1;
            sendSet();
        } else if (ev.key == kKeyBackspace && !nameText.empty()) {
            do
                nameText.pop_back(); // ganze UTF-8-Zeichen löschen
            while (!nameText.empty() && ((unsigned char)nameText.back() & 0xc0) == 0x80);
            redraw();
        }
        return true;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (songView && editName < 0) {
            if ((unsigned char)ev.string[0] >= 0x20 && songText.size() + std::strlen(ev.string) <= 48)
                songText += ev.string;
            redraw();
            return true;
        }
        if (editName < 0)
            return OffbeatKitUI::onCharacterInput(ev);
        if ((unsigned char)ev.string[0] >= 0x20 && nameText.size() + std::strlen(ev.string) < sizeof(TrackConf::name) - 1)
            nameText += ev.string;
        redraw();
        return true;
    }

    void onButton(int b) override
    {
        const int pat = editPattern();
        if (b >= kBtnStep && b < kBtnStep + kTracks * kMaxSteps) {
            Step& st = model.patterns[pat].tracks[(b - kBtnStep) / kMaxSteps].steps[(b - kBtnStep) % kMaxSteps];
            if (velMode) {
                static const uint8_t kNext[4] = { kVelNormal, kVelOff, kVelLoud, kVelGhost }; // aus→normal→laut→Ghost→aus
                st.vel = kNext[st.vel & 3];
            } else {
                st.vel = st.vel ? kVelOff : kVelNormal;
                if (!st.vel)
                    st = Step();
            }
            sendSet();
        } else if (b >= kBtnRoll && b < kBtnRoll + kKeyCount * kMaxSteps && rollTrack >= 0) {
            const int offset = (b - kBtnRoll) / kMaxSteps + kKeyLow, si = (b - kBtnRoll) % kMaxSteps;
            Step& st = model.patterns[pat].tracks[rollTrack].steps[si];
            const uint64_t bit = keyBit(offset);
            uint64_t keys = st.vel ? (st.keys ? st.keys : keyBit(0)) : 0;
            if (velMode && (keys & bit)) { // STÄRKE: Stärke des Schritts wechseln
                static const uint8_t kNext[4] = { kVelNormal, kVelOff, kVelLoud, kVelGhost };
                st.vel = kNext[st.vel & 3];
                if (!st.vel)
                    st = Step();
                sendSet();
                return;
            }
            keys ^= bit;
            if (keys & bit) { // gesetzte Note anspielen (auf dem Kanal der Spur)
                const TrackConf& c = model.tracks[rollTrack];
                previewChannel = c.channel - 1;
                previewNote = c.note + offset;
                if (previewNote >= 0 && previewNote < 128)
                    sendNote((uint8_t)previewChannel, (uint8_t)previewNote, 100);
                else
                    previewNote = -1;
            }
            if (keys == 0) {
                st = Step();
            } else {
                if (st.vel == kVelOff)
                    st.vel = kVelNormal;
                st.keys = keys == keyBit(0) ? 0 : keys;
            }
            sendSet();
        } else if (b == kBtnOctDown || b == kBtnOctUp) {
            rollView = std::max(kKeyLow, std::min(0, rollView + (b == kBtnOctUp ? 12 : -12)));
            redraw();
        } else if (b == kBtnRollBack) {
            rollTrack = -1;
            redraw();
        } else if (b == kBtnSongs) {
            songView = !songView;
            if (songView)
                songs = listSongs();
            redraw();
        } else if (b == kBtnSongSave) {
            std::string err;
            if (saveSong(songText, setToJson(model), err)) {
                songs = listSongs();
                showStatus("gespeichert: " + songText);
            } else
                showStatus(validSongName(songText) ? err : "Name eingeben (ohne / \\ : * ? \" < > |)");
        } else if (b >= kBtnSongLoad && b < kBtnSongLoad + (int)songs.size()) {
            std::string json, err;
            if (loadSong(songs[b - kBtnSongLoad], json, err) && applyJsonText(model, json.c_str(), 0, err)) {
                songText = songs[b - kBtnSongLoad];
                sendSet();
                showStatus("geladen: " + songText);
            } else
                showStatus(err);
        } else if (b == kBtnChainOn) {
            model.chainOn = !model.chainOn;
            sendSet();
        } else if (b == kBtnChainClear) {
            model.chainLen = 0;
            sendSet();
        } else if (b >= kBtnChainAdd && b < kBtnChainAdd + kPatterns) {
            if (model.chainLen < kMaxChain)
                model.chain[model.chainLen++] = { b - kBtnChainAdd, 4 };
            sendSet();
        } else if (b >= kBtnChainDown && b < kBtnChainDel + kMaxChain) {
            const int i = (b - kBtnChainDown) % 100;
            if (i < model.chainLen) {
                if (b >= kBtnChainDel)
                    std::copy(model.chain + i + 1, model.chain + model.chainLen--, model.chain + i);
                else
                    model.chain[i].bars = std::max(1, std::min(64, model.chain[i].bars + (b >= kBtnChainUp ? 1 : -1)));
                sendSet();
            }
        } else if (b == kBtnRollMute && rollTrack >= 0) {
            setValue(kMute1 + rollTrack, muted(rollTrack) ? 0 : 1, true);
        } else if (b == kBtnRollSolo && rollTrack >= 0) {
            model.tracks[rollTrack].solo = !model.tracks[rollTrack].solo;
            sendSet();
        } else if (b == kBtnVelMode) {
            velMode = !velMode;
            redraw();
        } else if (b >= kBtnMute && b < kBtnMute + kTracks) {
            setValue(kMute1 + b - kBtnMute, muted(b - kBtnMute) ? 0 : 1, true);
        } else if (b >= kBtnSolo && b < kBtnSolo + kTracks) {
            model.tracks[b - kBtnSolo].solo = !model.tracks[b - kBtnSolo].solo;
            sendSet();
        } else if (b >= kBtnLenDown && b < kBtnLenUp + kTracks) {
            const bool up = b >= kBtnLenUp;
            PatternTrack& pt = model.patterns[pat].tracks[b - (up ? kBtnLenUp : kBtnLenDown)];
            pt.length = std::max(1, std::min(kMaxSteps, pt.length + (up ? 1 : -1)));
            sendSet();
        } else if (b >= kBtnRotL && b < kBtnRotR + kTracks) {
            const bool right = b >= kBtnRotR;
            PatternTrack& pt = model.patterns[pat].tracks[b - (right ? kBtnRotR : kBtnRotL)];
            if (right)
                std::rotate(pt.steps, pt.steps + pt.length - 1, pt.steps + pt.length);
            else
                std::rotate(pt.steps, pt.steps + 1, pt.steps + pt.length);
            sendSet();
        } else if (b >= kBtnChannel && b < kBtnSound + kTracks) {
            adjustField(b, 1);
        } else if (b >= kBtnPattern && b < kBtnPattern + kPatterns) {
            setValue(kPattern, (float)(b - kBtnPattern), true);
        } else if (b >= kBtnPage && b < kBtnPage + 4) {
            if ((b - kBtnPage) * 16 < maxLength())
                page = b - kBtnPage;
            redraw();
        } else if (b == kBtnPlay) {
            setValue(kPlay, values[kPlaying] >= 0.5f ? 0 : 1, true);
        } else if (b == kBtnFill) {
            fillHeld = true;
            setValue(kFill, 1, true);
        } else if (b == kBtnRemote) {
            if (values[kRemote] >= 0.5f) {
                fetchState(); // letzte Änderungen vom iPad übernehmen, bevor der Server stoppt
                sendSet();
            }
            setValue(kRemote, values[kRemote] >= 0.5f ? 0 : 1, true);
        } else if (b == kBtnCopyPattern || b == kBtnCopySet) {
            const std::string json = b == kBtnCopySet ? setToJson(model) : patternToJson(model, pat);
            showStatus(getWindow().setClipboard("text/plain", json.c_str(), json.size()) ? "kopiert" : "Zwischenablage nicht verfügbar");
        } else if (b == kBtnPasteGrid) {
            size_t size = 0;
            const char* data = (const char*)getWindow().getClipboard(size);
            std::string err;
            if (data && size && applyJsonText(model, std::string(data, size).c_str(), pat, err)) {
                sendSet();
                showStatus("eingefügt");
            } else {
                showStatus(err.empty() ? "Zwischenablage leer" : err);
            }
        } else if (b == kBtnClear) {
            for (PatternTrack& pt : model.patterns[pat].tracks)
                for (Step& st : pt.steps)
                    st = Step();
            sendSet();
        }
    }

private:
    NanoImage gearbox;
    Set model;
    int page = 0, editName = -1;
    std::string nameText;
    bool fillHeld = false, velMode = false;
    bool songView = false;          // Song-Ansicht statt Raster
    std::string songText;           // Name zum Speichern
    std::vector<std::string> songs; // gespeicherte Songs (beim Öffnen/Speichern gelesen)
    int rollTrack = -1, rollView = 0; // offene Piano-Roll (Spur, unterster Versatz)
    int previewNote = -1, previewChannel = 0;
    uint32_t fetchedRevision = 0;
    std::string address;
    Clock::time_point addressTime;

    // Bearbeitet wird das Pattern, das der DSP gewählt hat (Anzeige-Wert): auch wenn eine Auswahl vom iPad
    // den Host-Parameter nicht erreicht hat, bearbeitet die Oberfläche dasselbe Pattern, das gleich spielt.
    int editPattern() const { return (int)values[kQueuedPattern]; }
    bool muted(int t) const { return ((int)values[kMuteState] >> t) & 1; }

    int maxLength() const
    {
        int m = 1;
        for (const PatternTrack& pt : model.patterns[editPattern()].tracks)
            m = std::max(m, pt.length);
        return m;
    }

    void sendSet()
    {
        setState("set", setToJson(model).c_str());
        redraw();
    }

    void fetchState()
    {
        const int port = (int)values[kRemotePort];
        char path[32];
        std::snprintf(path, sizeof(path), "/state?pin=%04d", (int)values[kRemotePin]);
        std::string body, err;
        if (port > 0 && localGet(port, path, body) && applyJsonText(model, body.c_str(), 0, err))
            redraw();
    }

    // Kurzname für das SOUND-Feld: Kategorie abgekürzt, z. B. „Bass Rubber“
    static std::string soundLabel(const TrackConf& c, int program)
    {
        if (program < 0)
            return "\xe2\x80\x93";
        const char* n = soundName(c.synth, program);
        std::string s = n ? n : "Prog " + std::to_string(program + 1);
        if (const size_t d = s.find(" - "); d != std::string::npos)
            s = s.substr(0, d) + " " + s.substr(d + 3);
        if (const size_t p = s.find(" ("); p != std::string::npos)
            s.resize(p);
        if (s.size() > 13)
            s.resize(13);
        return s;
    }

    // SOUND: eine Liste aus „–“, allen Engine- und allen Topline-Presets; die Wahl legt auch den Synth der Spur fest
    void adjustSound(int t, int dir)
    {
        TrackConf& c = model.tracks[t];
        int& prog = model.patterns[editPattern()].tracks[t].program;
        const int total = 1 + kEngineSoundCount + kToplineSoundCount;
        int i = prog < 0 ? 0 : c.synth == 2 ? 1 + kEngineSoundCount + std::min(prog, kToplineSoundCount - 1)
                                             : 1 + std::min(prog, kEngineSoundCount - 1);
        i = (i + dir + total) % total;
        if (i == 0)
            prog = -1;
        else if (i <= kEngineSoundCount)
            c.synth = 1, prog = i - 1;
        else
            c.synth = 2, prog = i - 1 - kEngineSoundCount;
        const char* n = soundName(c.synth, prog);
        showStatus(std::string(c.name) + ": " + (n ? (c.synth == 1 ? "Engine \xc2\xb7 " : "Topline \xc2\xb7 ") + std::string(n) : "kein Sound-Wechsel"));
        sendSet();
    }

    void adjustField(int b, int dir)
    {
        if (b >= kBtnSound) {
            adjustSound(b - kBtnSound, dir);
            return;
        }
        TrackConf& c = model.tracks[(b - kBtnChannel) % 100];
        if (b < kBtnNote) c.channel = std::max(1, std::min(16, c.channel + dir));
        else if (b < kBtnGate) c.note = std::max(0, std::min(127, c.note + dir));
        else if (b < kBtnVolume) c.gate = std::max(0.05f, std::min(0.95f, std::round((c.gate + 0.05f * dir) * 20) / 20));
        else c.volume = std::max(0.0f, std::min(1.0f, std::round((c.volume + 0.05f * dir) * 20) / 20));
        sendSet();
    }

    void field(int id, float x, float y, float w, float h, const char* label, const char* value)
    {
        addHit(-1, id, x, y, w, h, kButton, y + h);
        beginPath();
        roundedRect(x, y, w, h, 4);
        fillColor(kit::withAlpha(th.plate, 0.9f));
        fill();
        strokeColor(kit::withAlpha(th.brand, 0.35f));
        strokeWidth(1);
        stroke();
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(7.5f);
        fillColor(th.dim);
        text(x + w / 2, y + 3, label, nullptr);
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        fontSize(11);
        fillColor(th.text);
        text(x + w / 2, y + h - 3, value, nullptr);
    }

    void drawGrid()
    {
        const int pat = editPattern();
        char title[64];
        std::snprintf(title, sizeof(title), "RASTER  \xc2\xb7  PATTERN %c  \xc2\xb7  SCHRITTE %d\xe2\x80\x93%d", 'A' + pat, page * 16 + 1, page * 16 + 16);
        drawPanel(kMargin, kGridY, kW - 2 * kMargin, kLabelH + 8 * kRowH + 14, title, th.brand);
        bool anySolo = false;
        for (const TrackConf& c : model.tracks)
            anySolo |= c.solo;
        for (int t = 0; t < kTracks; ++t) {
            const TrackConf& c = model.tracks[t];
            const PatternTrack& pt = model.patterns[pat].tracks[t];
            const float y = kGridY + kLabelH + 6 + t * kRowH, h = kRowH - 8;
            float x = kMargin + 12;
            const bool isMuted = muted(t), silent = isMuted || (anySolo && !c.solo);

            // Name
            addHit(-1, kBtnName + t, x, y, kNameW, h, kButton, y + h);
            beginPath();
            roundedRect(x, y, kNameW, h, 5);
            fillColor(kit::withAlpha(editName == t ? th.track : th.plate, 0.95f));
            fill();
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fontSize(12);
            fillColor(silent ? th.dim : th.text);
            const std::string name = editName == t ? nameText + "|" : std::string(c.name);
            text(x + 8, y + h / 2, name.c_str(), nullptr);
            x += kNameW + 6;

            pill(kBtnMute + t, x, y + 6, kSmallW, h - 12, "M", th.brand, isMuted);
            x += kSmallW + 4;
            pill(kBtnSolo + t, x, y + 6, kSmallW, h - 12, "S", th.light, c.solo);
            x += kSmallW + 8;

            char v[16];
            std::snprintf(v, sizeof(v), "%d", c.channel);
            field(kBtnChannel + t, x, y, kFieldW - 8, h, "KANAL", v);
            x += kFieldW - 4;
            std::snprintf(v, sizeof(v), "%s", noteName(c.note).c_str());
            field(kBtnNote + t, x, y, kFieldW, h, "NOTE", v);
            x += kFieldW + 4;
            std::snprintf(v, sizeof(v), "%d%%", (int)std::lround(c.gate * 100));
            field(kBtnGate + t, x, y, kFieldW, h, "GATE", v);
            x += kFieldW + 4;
            std::snprintf(v, sizeof(v), "%d%%", (int)std::lround(c.volume * 100));
            field(kBtnVolume + t, x, y, kFieldW, h, "VOL", v);
            x += kFieldW + 4;
            field(kBtnSound + t, x, y, kSoundW, h, "SOUND", soundLabel(c, pt.program).c_str());
            x += kSoundW + 12;

            // Schritte der Seite
            const int cursor = values[kPlaying] >= 0.5f && (int)values[kPlayingPattern] == pat ? (int)values[kStep1 + t] : -1;
            for (int k = 0; k < 16; ++k) {
                const int s = page * 16 + k;
                const float cx = x + k * (kCellW + kCellGap) + (k / 4) * 6;
                const Step& st = pt.steps[s];
                const bool inside = s < pt.length;
                if (inside)
                    addHit(-1, kBtnStep + t * kMaxSteps + s, cx, y, kCellW, h, kButton, y + h);
                const float alpha = st.vel == kVelLoud ? 1.0f : st.vel == kVelNormal ? 0.7f : st.vel == kVelGhost ? 0.32f : 0.0f;
                if (st.vel && inside && !silent && st.vel == kVelLoud) { // Glühen für laute Schritte
                    beginPath();
                    rect(cx - 8, y - 8, kCellW + 16, h + 16);
                    fillPaint(boxGradient(cx, y, kCellW, h, 6, 10, kit::withAlpha(th.brand, 0.45f), kit::withAlpha(th.brand, 0.0f)));
                    fill();
                }
                beginPath();
                roundedRect(cx, y, kCellW, h, 5);
                fillColor(!inside ? kit::withAlpha(th.plate, 0.35f)
                          : st.vel ? kit::withAlpha(silent ? th.dim : th.brand, alpha)
                                   : kit::withAlpha(th.plate, (k % 4) ? 0.9f : 1.0f));
                fill();
                strokeColor(kit::withAlpha(s == cursor ? th.text : th.brand, s == cursor ? 1.0f : inside ? 0.35f : 0.1f));
                strokeWidth(s == cursor ? 2.0f : 1.0f);
                stroke();
                if (c.melody && st.vel && inside) { // Melodie: tiefste Note, +n bei Akkord
                    int lowest = c.note, extra = -1;
                    if (st.keys) {
                        extra = -1;
                        for (int k = kKeyCount - 1; k >= 0; --k)
                            if (st.keys >> k & 1)
                                lowest = c.note + k + kKeyLow, ++extra;
                    }
                    textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                    fontSize(9);
                    fillColor(th.night);
                    text(cx + kCellW / 2, y + h / 2 - (extra > 0 ? 5 : 0), noteName(std::max(0, std::min(127, lowest))).c_str(), nullptr);
                    if (extra > 0) {
                        char more[8];
                        std::snprintf(more, sizeof(more), "+%d", extra);
                        text(cx + kCellW / 2, y + h / 2 + 7, more, nullptr);
                    }
                }
                if (st.tie && inside) { // Tie: Balken zum nächsten Schritt
                    beginPath();
                    rect(cx + kCellW - 6, y + h / 2 - 3, 6 + kCellGap + 4, 6);
                    fillColor(th.light);
                    fill();
                }
            }
            x += 16 * (kCellW + kCellGap) + 18 + 12;

            // Länge und Rotieren
            pill(kBtnLenDown + t, x, y + 6, 22, h - 12, "-", th.brand, false);
            std::snprintf(v, sizeof(v), "%d", pt.length);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fontSize(12);
            fillColor(th.text);
            text(x + 38, y + h / 2, v, nullptr);
            pill(kBtnLenUp + t, x + 54, y + 6, 22, h - 12, "+", th.brand, false);
            x += 86;
            pill(kBtnRotL + t, x, y + 6, 24, h - 12, "\xe2\x97\x80", th.brand, false);
            pill(kBtnRotR + t, x + 28, y + 6, 24, h - 12, "\xe2\x96\xb6", th.brand, false);
        }
    }

    // Songs (links) und Ablauf (rechts) statt des Rasters
    void drawSongs()
    {
        const float px = kMargin, py = kGridY, pw = kW - 2 * kMargin, ph = kLabelH + 8 * kRowH + 14;
        drawPanel(px, py, pw, ph, "SONGS  \xc2\xb7  ABLAUF", th.brand);
        const float top = py + kLabelH + 6, colW = 420;
        // Name + Speichern
        beginPath();
        roundedRect(px + 14, top, colW - 120, 30, 5);
        fillColor(kit::withAlpha(th.track, 0.95f));
        fill();
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fontSize(13);
        fillColor(songText.empty() ? th.dim : th.text);
        text(px + 24, top + 15, songText.empty() ? "Name tippen, Enter speichert" : (songText + "|").c_str(), nullptr);
        pill(kBtnSongSave, px + colW - 96, top, 96, 30, "Speichern", th.brand, false);
        fontSize(11);
        fillColor(th.dim);
        text(px + 14, top + 46, songs.empty() ? "Noch keine Songs in ~/Music/Offbeat Gearbox" : "Klick l\xc3\xa4" "dt (sofort):", nullptr);
        for (int i = 0; i < (int)songs.size() && i < 24; ++i) {
            const float x = px + 14 + (i / 12) * (colW / 2 + 4), y = top + 60 + (i % 12) * 27;
            pill(kBtnSongLoad + i, x, y, colW / 2 - 4, 23, songs[i].c_str(), th.brand, songs[i] == songText);
        }

        // Ablauf
        const float ax = px + colW + 40, aw = pw - colW - 54;
        pill(kBtnChainOn, ax, top, 140, 30, model.chainOn ? "Ablauf AN" : "Ablauf AUS", th.light, model.chainOn);
        pill(kBtnChainClear, ax + 150, top, 90, 30, "Leeren", th.brand, false);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fontSize(11);
        fillColor(th.dim);
        text(ax + 254, top + 9, "startet mit PLAY \xc2\xb7 Pattern-Knopf", nullptr);
        text(ax + 254, top + 23, "\xc3\xbc" "bernimmt live bis zum n\xc3\xa4" "chsten Start", nullptr);
        text(ax, top + 46, "Anh\xc3\xa4ngen (4 Takte):", nullptr);
        for (int p = 0; p < kPatterns; ++p) {
            const char name[2] = { (char)('A' + p), 0 };
            pill(kBtnChainAdd + p, ax + 130 + p * 40, top + 34, 34, 24, name, th.brand, false);
        }
        const float cw = 150, ch = 30;
        const int perRow = std::max(1, (int)(aw / (cw + 8)));
        for (int i = 0; i < model.chainLen; ++i) {
            const float x = ax + (i % perRow) * (cw + 8), y = top + 72 + (i / perRow) * (ch + 8);
            beginPath();
            roundedRect(x, y, cw, ch, 6);
            fillColor(kit::withAlpha(th.plate, 0.95f));
            fill();
            strokeColor(kit::withAlpha(th.brand, 0.5f));
            strokeWidth(1);
            stroke();
            char label[24];
            std::snprintf(label, sizeof(label), "%d. %c \xc3\x97%d", i + 1, 'A' + model.chain[i].pattern, model.chain[i].bars);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fontSize(12);
            fillColor(th.text);
            text(x + 8, y + ch / 2, label, nullptr);
            pill(kBtnChainDown + i, x + cw - 76, y + 4, 22, ch - 8, "-", th.brand, false);
            pill(kBtnChainUp + i, x + cw - 50, y + 4, 22, ch - 8, "+", th.brand, false);
            pill(kBtnChainDel + i, x + cw - 24, y + 4, 20, ch - 8, "\xc3\x97", th.brand, false);
        }
    }

    // Piano-Roll einer Melodie-Spur: 24 Tonhöhen (ab rollView) × 16 Schritte der Seite.
    void drawRoll()
    {
        const int pat = editPattern();
        const TrackConf& c = model.tracks[rollTrack];
        const PatternTrack& pt = model.patterns[pat].tracks[rollTrack];
        const float px = kMargin, py = kGridY, pw = kW - 2 * kMargin, ph = kLabelH + 8 * kRowH + 14;
        char title[96];
        std::snprintf(title, sizeof(title), "%s  \xc2\xb7  PIANO-ROLL  \xc2\xb7  PATTERN %c  \xc2\xb7  SCHRITTE %d\xe2\x80\x93%d",
                      c.name, 'A' + pat, page * 16 + 1, page * 16 + 16);
        drawPanel(px, py, pw, ph, title, th.brand);

        const float by = py + kLabelH + 2;
        pill(kBtnRollMute, px + 12, by, 30, 22, "M", th.brand, muted(rollTrack));
        pill(kBtnRollSolo, px + 46, by, 30, 22, "S", th.light, c.solo);
        pill(kBtnOctDown, px + pw - 12 - 3 * 96, by, 88, 22, "Okt -", th.brand, false);
        pill(kBtnOctUp, px + pw - 12 - 2 * 96, by, 88, 22, "Okt +", th.brand, false);
        pill(kBtnRollBack, px + pw - 12 - 96, by, 96, 22, "Raster", th.brand, true);

        static const bool kBlack[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
        const float top = by + 30, rowH = (py + ph - 20 - top) / 24, labelW = 60, x0 = px + 12 + labelW + 6;
        const float cellW = 56, gap = 3;
        const int cursor = values[kPlaying] >= 0.5f && (int)values[kPlayingPattern] == pat ? (int)values[kStep1 + rollTrack] : -1;
        for (int r = 0; r < 24; ++r) {
            const int offset = rollView + 23 - r, note = c.note + offset; // oben die höchste Tonhöhe
            const float y = top + r * rowH;
            const bool black = note >= 0 && kBlack[note % 12];
            beginPath(); // Zeilenhintergrund wie eine Klaviatur
            rect(px + 12, y, pw - 24, rowH - 1);
            fillColor(kit::withAlpha(black ? th.night : th.plate, black ? 0.9f : 0.6f));
            fill();
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fontSize(9);
            fillColor(note % 12 == 0 ? th.text : th.dim);
            if (note >= 0 && note < 128)
                text(px + 18, y + rowH / 2, noteName(note).c_str(), nullptr);
            for (int k = 0; k < 16; ++k) {
                const int si = page * 16 + k;
                const float cx = x0 + k * (cellW + gap) + (k / 4) * 6;
                const bool inside = si < pt.length && note >= 0 && note < 128;
                const Step& st = pt.steps[si];
                const bool on = st.vel && ((st.keys ? st.keys : keyBit(0)) & keyBit(offset));
                if (inside)
                    addHit(-1, kBtnRoll + (offset - kKeyLow) * kMaxSteps + si, cx, y, cellW, rowH - 1, kButton, y + rowH);
                if (on) {
                    const float alpha = st.vel == kVelLoud ? 1.0f : st.vel == kVelNormal ? 0.75f : 0.35f;
                    beginPath();
                    roundedRect(cx + 1, y + 1, cellW - 2, rowH - 3, 3);
                    fillColor(kit::withAlpha(th.brand, alpha));
                    fill();
                    if (st.tie) { // Tie: Verbindung zum nächsten Schritt
                        beginPath();
                        rect(cx + cellW - 4, y + rowH / 2 - 2, gap + 8, 4);
                        fillColor(th.light);
                        fill();
                    }
                } else if (!inside) {
                    beginPath();
                    rect(cx, y, cellW, rowH - 1);
                    fillColor(kit::withAlpha(th.night, 0.6f));
                    fill();
                }
            }
        }
        for (int k = 0; k <= 16; ++k) { // Schritt- und Schlaglinien, Cursor
            const float cx = x0 + k * (cellW + gap) + (std::min(k, 15) / 4) * 6 - (k == 16 ? gap : 0) - 1.5f;
            beginPath();
            moveTo(cx, top);
            lineTo(cx, top + 24 * rowH);
            strokeColor(kit::withAlpha(th.brand, k % 4 == 0 ? 0.45f : 0.15f));
            strokeWidth(1);
            stroke();
        }
        if (cursor >= page * 16 && cursor < page * 16 + 16) {
            const int k = cursor - page * 16;
            const float cx = x0 + k * (cellW + gap) + (k / 4) * 6;
            beginPath();
            rect(cx, top, cellW, 24 * rowH);
            strokeColor(th.text);
            strokeWidth(1.5f);
            stroke();
        }
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(9);
        fillColor(th.dim);
        for (int k = 0; k < 16; ++k) {
            char num[4];
            std::snprintf(num, sizeof(num), "%d", page * 16 + k + 1);
            text(x0 + k * (cellW + gap) + (k / 4) * 6 + cellW / 2, top + 24 * rowH + 3, num, nullptr);
        }
    }

    void drawRemote()
    {
        drawPanel(kMargin, kRemoteY, kW - 2 * kMargin, kH - kRemoteY - 12, "REMOTE  \xc2\xb7  IPAD", th.brand);
        const float y = kRemoteY + kLabelH + 14;
        const bool on = values[kRemote] >= 0.5f;
        pill(kBtnRemote, kMargin + 14, y, 80, 30, on ? "An" : "Aus", th.brand, on);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fontSize(14);
        fillColor(on ? th.text : th.dim);
        char line[160];
        const int port = (int)values[kRemotePort];
        if (on && port > 0) {
            if (address.empty() || Clock::now() - addressTime > std::chrono::seconds(3)) {
                address = localAddress();
                addressTime = Clock::now();
            }
            std::snprintf(line, sizeof(line), "http://%s:%d    PIN %04d    %d %s", address.empty() ? "?" : address.c_str(), port,
                          (int)values[kRemotePin], (int)values[kRemoteClients], (int)values[kRemoteClients] == 1 ? "Gerät" : "Geräte");
        } else {
            std::snprintf(line, sizeof(line), on ? "startet \xe2\x80\xa6" : "aus \xe2\x80\x93 einschalten, dann die Adresse in Safari auf dem iPad \xc3\xb6""ffnen (selbes WLAN)");
        }
        text(kMargin + 110, y + 15, line, nullptr);

        const float bx = kW - kMargin - 14 - 4 * 112;
        pill(kBtnClear, bx, y, 104, 30, "Leeren", th.brand, false);
        pill(kBtnCopyPattern, bx + 112, y, 104, 30, "Kopieren", th.brand, false);
        pill(kBtnCopySet, bx + 224, y, 104, 30, "Set kopieren", th.brand, false);
        pill(kBtnPasteGrid, bx + 336, y, 104, 30, "EINF\xc3\x9cGEN", th.brand, false);
        if (!status.empty() && Clock::now() < statusUntil) {
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            fontSize(11);
            fillColor(th.text);
            text(bx - 10, y + 15, status.c_str(), nullptr);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GearboxUI)
};

UI* createUI() { return new GearboxUI(); }

END_NAMESPACE_DISTRHO
