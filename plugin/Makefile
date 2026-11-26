#!/usr/bin/make -f
# DPF-Build des Plugins. Aufruf über das Makefile im Projektordner (make -C plugin).

NAME = offbeat-gearbox

FILES_DSP = \
	GearboxPlugin.cpp \
	../src/dsp/Sequencer.cpp \
	../src/dsp/Model.cpp \
	../src/dsp/Json.cpp \
	../src/dsp/Params.cpp \
	../src/dsp/ParamText.cpp \
	../src/remote/Core.cpp \
	../src/remote/Server.cpp

# Model/Params/Server nutzt auch die UI; sie steckt bei VST3 und LV2 (eine Datei) im selben Binary.
FILES_UI = GearboxUI.cpp

DPF_PATH = ../dpf
DPF_TARGET_DIR = ../bin
DPF_BUILD_DIR ?= ../build/offbeat-gearbox

include ../dpf/Makefile.plugins.mk

BUILD_CXX_FLAGS += $(EXTRA_FLAGS) -std=gnu++17 -I../src/dsp -I../src/remote -I$(DPF_BUILD_DIR)
ifeq ($(WINDOWS),true)
LINK_FLAGS += -lws2_32
endif

# Versionsnummer für die Oberfläche (klein unter dem Logo), aus git describe beim Build
OFFBEAT_VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
BUILD_CXX_FLAGS += -DOFFBEAT_VERSION='"$(OFFBEAT_VERSION)"'

# Eingebettete Dateien: iPad-Seite + Manifest, Bild der Oberfläche
$(DPF_BUILD_DIR)/RemotePage.h: ../src/remote/page.html ../src/remote/manifest.webmanifest
	-@mkdir -p $(DPF_BUILD_DIR)
	cd ../src/remote && (xxd -i page.html && xxd -i manifest.webmanifest) > $(abspath $@)

$(DPF_BUILD_DIR)/GearboxImage.h: ../assets/gearbox.png
	-@mkdir -p $(DPF_BUILD_DIR)
	cd ../assets && xxd -i gearbox.png > $(abspath $@)

$(DPF_BUILD_DIR)/../src/remote/Server.cpp.o: $(DPF_BUILD_DIR)/RemotePage.h
$(DPF_BUILD_DIR)/GearboxUI.cpp.o: $(DPF_BUILD_DIR)/GearboxImage.h

TARGETS += vst3
TARGETS += lv2
TARGETS += vst2
TARGETS += clap
ifeq ($(MACOS),true)
TARGETS += au
endif

all: $(TARGETS)
