# Offbeat Gearbox – Build für Plugin (VST3, VST2, AU, CLAP, LV2) und Testwerkzeug gearbox-render.
#   make             Plugin + gearbox-render (macOS: Universal-Binary; make UNIVERSAL=false nur eigene Architektur)
#   make install     macOS: nach ~/Library/Audio/Plug-Ins kopieren und ad-hoc signieren
#   make check       Abnahme: Sequencer (gearbox-render --check-all) und Fernbedienung (tools/remote-test.sh, curl)
#   make standalone  Standalone-App (für Bilder der Oberfläche)
# Windows (MSYS2/MinGW) und Linux: `make` baut nach bin/.

NAME = offbeat-gearbox
CXX ?= c++
SRC = src/dsp/Sequencer.cpp src/dsp/Model.cpp src/dsp/Json.cpp src/dsp/Params.cpp src/remote/Core.cpp src/remote/Server.cpp
HDR = $(wildcard src/dsp/*.hpp src/remote/*.hpp)
PLUGINS_DIR = $(HOME)/Library/Audio/Plug-Ins
ifeq ($(OS),Windows_NT)
NET_LIBS = -lws2_32
else
NET_LIBS = -pthread
endif

UNIVERSAL ?= true
ifeq ($(shell uname -s)$(UNIVERSAL),Darwintrue)
# Universal-Binary: läuft auf Intel- und Apple-Silicon-Macs. NOOPT: ohne x86-only-Flags (-mfpmath=sse).
ARCH_FLAGS = -arch x86_64 -arch arm64 -mmacosx-version-min=10.15
PLUGIN_ENV = CFLAGS="$(ARCH_FLAGS)" CXXFLAGS="$(ARCH_FLAGS)" LDFLAGS="$(ARCH_FLAGS)" NOOPT=true
endif

all: plugin render

plugin:
	$(PLUGIN_ENV) $(MAKE) -C plugin
	$(MAKE) -C dpf/utils/lv2-ttl-generator
	dpf/utils/generate-ttl.sh $(CURDIR)/bin

standalone: # eigener Build-Ordner: hat zwei (stumme) Audio-Ausgänge
	$(PLUGIN_ENV) $(MAKE) -C plugin jack DPF_BUILD_DIR=../build/offbeat-gearbox-standalone EXTRA_FLAGS=-DGEARBOX_STANDALONE

render: bin/gearbox-render

build/RemotePage.h: src/remote/page.html src/remote/manifest.webmanifest
	@mkdir -p build
	cd src/remote && (xxd -i page.html && xxd -i manifest.webmanifest) > ../../build/RemotePage.h

bin/gearbox-render: tools/gearbox-render.cpp $(SRC) $(HDR) build/RemotePage.h
	@mkdir -p bin
	$(CXX) -std=gnu++17 -O2 -Wall -Wextra -DGEARBOX_SERVER -Isrc/dsp -Isrc/remote -Ibuild tools/gearbox-render.cpp $(SRC) -o $@ $(NET_LIBS)

check: render
	bin/gearbox-render --check-all
	tools/remote-test.sh

install: plugin
	rm -rf "$(PLUGINS_DIR)/VST3/$(NAME).vst3" "$(PLUGINS_DIR)/LV2/$(NAME).lv2" "$(PLUGINS_DIR)/VST/$(NAME).vst" \
	       "$(PLUGINS_DIR)/Components/$(NAME).component" "$(PLUGINS_DIR)/CLAP/$(NAME).clap"
	mkdir -p "$(PLUGINS_DIR)/VST3" "$(PLUGINS_DIR)/LV2" "$(PLUGINS_DIR)/VST" "$(PLUGINS_DIR)/Components" "$(PLUGINS_DIR)/CLAP"
	cp -R bin/$(NAME).vst3 "$(PLUGINS_DIR)/VST3/"
	cp -R bin/$(NAME).lv2 "$(PLUGINS_DIR)/LV2/"
	cp -R bin/$(NAME).vst "$(PLUGINS_DIR)/VST/"
	cp -R bin/$(NAME).component "$(PLUGINS_DIR)/Components/"
	cp -R bin/$(NAME).clap "$(PLUGINS_DIR)/CLAP/"
	codesign --force --deep -s - "$(PLUGINS_DIR)/VST3/$(NAME).vst3"
	codesign --force -s - "$(PLUGINS_DIR)/LV2/$(NAME).lv2/$(NAME).dylib"  # LV2-Ordner ist kein Bundle
	codesign --force --deep -s - "$(PLUGINS_DIR)/VST/$(NAME).vst"
	codesign --force --deep -s - "$(PLUGINS_DIR)/Components/$(NAME).component"
	codesign --force --deep -s - "$(PLUGINS_DIR)/CLAP/$(NAME).clap"
	killall -9 AudioComponentRegistrar 2>/dev/null || true  # AU-Liste neu einlesen

clean:
	rm -rf bin build

.PHONY: all plugin standalone render check install clean
