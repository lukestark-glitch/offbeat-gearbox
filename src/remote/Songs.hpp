// Songs: ganze Sets als JSON-Dateien in einem festen Ordner (~/Music/Offbeat Gearbox). Nie im Audio-Thread aufrufen.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace offbeat {

std::string songDir();                      // legt den Ordner bei Bedarf an
bool validSongName(const std::string& name); // keine Pfade, keine Steuerzeichen, 1–48 Bytes
std::vector<std::string> listSongs();       // Namen ohne .json, sortiert
bool saveSong(const std::string& name, const std::string& json, std::string& err);
bool loadSong(const std::string& name, std::string& json, std::string& err);

// Offbeat Link: hängt an das Set-JSON die Sounds der verbundenen Synths an ("sounds"), Loops von Horn werden in
// den Ordner „<Song> Loops“ kopiert. linkId = Kennung dieser Gearbox-Instanz.
std::string withSounds(const std::string& setJson, const std::string& name, uint32_t linkId);
// Schickt die Sounds eines geladenen Songs an die verbundenen Synths; Anzahl der Anfragen.
int sendSounds(const std::string& songJson, uint32_t linkId);

namespace link { struct Registry; }
link::Registry* linkRegistry(); // gemeinsames Register (einmal geöffnet), nullptr = nicht verfügbar

} // namespace offbeat
