// Songs: ganze Sets als JSON-Dateien in einem festen Ordner (~/Music/Offbeat Gearbox). Nie im Audio-Thread aufrufen.
#pragma once

#include <string>
#include <vector>

namespace offbeat {

std::string songDir();                      // legt den Ordner bei Bedarf an
bool validSongName(const std::string& name); // keine Pfade, keine Steuerzeichen, 1–48 Bytes
std::vector<std::string> listSongs();       // Namen ohne .json, sortiert
bool saveSong(const std::string& name, const std::string& json, std::string& err);
bool loadSong(const std::string& name, std::string& json, std::string& err);

} // namespace offbeat
