.SUFFIXES:

ROOT            := ../..
TARGET          := LocalSend3DS
BUILD           := build
CIA             := $(TARGET).cia
APP_ELF         := $(ROOT)/$(TARGET).elf
APP_SMDH        := $(ROOT)/$(TARGET).smdh
APP_ICON        := $(ROOT)/icon.png
BANNER_PNG      := $(BUILD)/banner.png
BANNER_WAV      := $(BUILD)/banner.wav
BANNER          := $(BUILD)/banner.bnr
ASSET_STAMP     := $(BUILD)/assets.stamp
BANNER_VALIDATOR := scripts/validate_banner.py

PYTHON          ?= python3
BANNERTOOL      ?= bannertool
MAKEROM         ?= makerom

.PHONY: all clean

all: $(CIA)

$(BUILD):
	@mkdir -p $@

$(ASSET_STAMP): scripts/generate_assets.py $(APP_ICON) | $(BUILD)
	@$(PYTHON) $< $(APP_ICON) $(BANNER_PNG) $(BANNER_WAV)
	@touch $@

$(BANNER_PNG) $(BANNER_WAV): $(ASSET_STAMP)

$(BANNER): $(BANNER_PNG) $(BANNER_WAV) $(BANNER_VALIDATOR)
	@$(BANNERTOOL) makebanner -i $(BANNER_PNG) -a $(BANNER_WAV) -o $@
	@$(PYTHON) $(BANNER_VALIDATOR) $@ $(BANNER_WAV)

$(CIA): $(APP_ELF) $(APP_SMDH) $(BANNER) native.rsf
	@$(MAKEROM) -f cia -o $@ -rsf native.rsf -target t -exefslogo \
		-elf $(APP_ELF) -icon $(APP_SMDH) -banner $(BANNER) \
		-major 1 -minor 0 -micro 0
	@echo "built ... $(CIA)"

clean:
	@rm -rf $(BUILD) $(CIA)
