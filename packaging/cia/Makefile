.SUFFIXES:
.NOTPARALLEL:

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

# Read the application version; CIA title versions have 6/6/4-bit components.
VERSION_HEADER  := $(ROOT)/include/config.h
VERSION_PARTS   := $(shell $(PYTHON) -c 'import pathlib, re, sys; \
    definitions = re.findall(r"^[ \t]*\#[ \t]*define[ \t]+LS3DS_APP_VERSION\b([^\r\n]*)", pathlib.Path(sys.argv[1]).read_text(), re.M); \
    match = re.fullmatch(r"\"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\"", definitions[0].strip()) if len(definitions) == 1 else None; \
    parts = tuple(map(int, match.groups())) if match else (); \
    print(*parts) if len(parts) == 3 and parts[0] <= 63 and parts[1] <= 63 and parts[2] <= 15 else None' "$(VERSION_HEADER)")
ifneq ($(words $(VERSION_PARTS)),3)
$(error Invalid application version in $(VERSION_HEADER): expected one LS3DS_APP_VERSION definition as "vMAJOR.MINOR.MICRO", no leading zeros, major/minor 0..63 and micro 0..15)
endif
CIA_VERSION_ARGS := -major $(word 1,$(VERSION_PARTS)) -minor $(word 2,$(VERSION_PARTS)) -micro $(word 3,$(VERSION_PARTS))

.PHONY: all clean print-version

all: $(CIA)

print-version:
	@echo "$(CIA_VERSION_ARGS)"

$(BUILD):
	@mkdir -p $@

$(ASSET_STAMP): scripts/generate_assets.py $(APP_ICON) | $(BUILD)
	@$(PYTHON) $< $(APP_ICON) $(BANNER_PNG) $(BANNER_WAV)
	@touch $@

$(BANNER_PNG) $(BANNER_WAV): $(ASSET_STAMP)
	@if [ ! -f "$(BANNER_PNG)" ] || [ ! -f "$(BANNER_WAV)" ]; then \
		$(PYTHON) scripts/generate_assets.py $(APP_ICON) $(BANNER_PNG) $(BANNER_WAV) && \
		touch $(ASSET_STAMP); \
	fi

$(BANNER): $(BANNER_PNG) $(BANNER_WAV) $(BANNER_VALIDATOR)
	@$(BANNERTOOL) makebanner -i $(BANNER_PNG) -a $(BANNER_WAV) -o $@
	@$(PYTHON) $(BANNER_VALIDATOR) $@ $(BANNER_WAV)

$(CIA): $(APP_ELF) $(APP_SMDH) $(BANNER) native.rsf $(VERSION_HEADER)
	@$(MAKEROM) -f cia -o $@ -rsf native.rsf -target t -exefslogo \
		-elf $(APP_ELF) -icon $(APP_SMDH) -banner $(BANNER) \
		$(CIA_VERSION_ARGS)
	@echo "built ... $(CIA)"

clean:
	@rm -rf $(BUILD) $(CIA)
