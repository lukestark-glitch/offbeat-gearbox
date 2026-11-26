// DPF-Beschreibung von Offbeat Gearbox: MIDI-Erzeuger (MIDI-Eingang optional, MIDI-Ausgang, kein Ton).
// Ohne Audio-Ein-/Ausgänge wird die AU-Version ein MIDI-Prozessor (aumi). Kein LADSPA (kann kein MIDI).
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "Offbeat"
#define DISTRHO_PLUGIN_NAME    "Offbeat Gearbox"
#define DISTRHO_PLUGIN_URI     "https://github.com/lukestark-glitch/offbeat-gearbox"
#define DISTRHO_PLUGIN_CLAP_ID "com.github.lukestark-glitch.offbeat-gearbox"

// VST2 zeigt sonst nur Parameter vor dem ersten Anzeige-Wert
#define DPF_VST_SHOW_PARAMETER_OUTPUTS 1

#define DISTRHO_PLUGIN_BRAND_ID  Ofbt
#define DISTRHO_PLUGIN_UNIQUE_ID OfbG

#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_IS_SYNTH      0
#define DISTRHO_PLUGIN_NUM_INPUTS    0
#ifdef GEARBOX_STANDALONE // Standalone-App braucht einen Audio-Ausgang (bleibt stumm)
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#else
#define DISTRHO_PLUGIN_NUM_OUTPUTS   0
#endif
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1 // Raster immer aktuell aus getState (auch Änderungen vom iPad)
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS     1
#define DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST 1 // iPad schaltet Mutes/Pattern/Play im Host

// LV2: Puffer für Nachrichten Oberfläche → DSP; das ganze Raster als JSON ist ~11 KB (Standard wäre 2 KB)
#define DISTRHO_PLUGIN_MINIMUM_BUFFER_SIZE 65536

#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_UI_USE_NANOVG           1
#define DISTRHO_UI_USER_RESIZABLE       0
#define DISTRHO_UI_DEFAULT_WIDTH        1220
#define DISTRHO_UI_DEFAULT_HEIGHT       720

#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:MIDIPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Generator"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
