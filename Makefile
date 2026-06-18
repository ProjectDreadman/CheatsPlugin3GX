# ══════════════════════════════════════════════════════════════════════════
#  CustomCheats — Luma3DS 3GX Plugin  ·  Makefile
#
#  Requirements:
#    · devkitPro with devkitARM  (https://devkitpro.org)
#    · libctru  (pacman -S 3ds-libctru)
#    · Luma3DS plugin SDK linker script (3gx.ld) installed alongside libctru
#
#  Build:
#    make                    → CustomCheats.3gx  (framebuffer GUI only)
#    make ENABLE_CITRO2D=1   → also compiles and links the experimental
#                              Citro2D GUI backend (requires citro2d +
#                              citro3d to be installed; pacman -S
#                              3ds-citro2d 3ds-citro3d). Off by default —
#                              see README.md "Graphical Interface" before
#                              enabling this in a build you distribute.
#    make clean              → remove build artefacts
#    make install SD_MOUNT=/path/to/sdcard
# ══════════════════════════════════════════════════════════════════════════

ifeq ($(strip $(DEVKITPRO)),)
  $(error "DEVKITPRO is not set.  Run: export DEVKITPRO=/opt/devkitpro")
endif
ifeq ($(strip $(DEVKITARM)),)
  $(error "DEVKITARM is not set.  Run: export DEVKITARM=$(DEVKITPRO)/devkitARM")
endif

TARGET   := CustomCheats
BUILD    := build
SOURCES  := source
INCLUDES := include $(DEVKITPRO)/libctru/include
DATA     := data

ENABLE_CITRO2D ?= 0

PREFIX   := $(DEVKITARM)/bin/arm-none-eabi-
CC       := $(PREFIX)gcc
LD       := $(PREFIX)gcc
OBJCOPY  := $(PREFIX)objcopy

PLUGIN_LD := $(DEVKITPRO)/libctru/lib/3gx.ld

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS   := $(ARCH) \
             -O2 -Wall -Wextra \
             -ffunction-sections -fdata-sections \
             -fno-builtin \
             $(addprefix -I,$(INCLUDES)) \
             -DARM11 -D_3DS

LDFLAGS  := $(ARCH) \
             -T$(PLUGIN_LD) \
             -Wl,--gc-sections \
             -Wl,-Map,$(BUILD)/$(TARGET).map \
             -nostartfiles \
             -L$(DEVKITPRO)/libctru/lib

LIBS     := -lctru -lm

# ─────────────────────────────────────────────
#  Source discovery
#
#  gui_citro2d.c is the ONLY file excluded from the build by default —
#  it #includes <citro2d.h>/<citro3d.h>, which most people building just
#  the framebuffer GUI won't have installed. Every other source file
#  (including the always-present gui_backend.c selector) is picked up
#  automatically via the wildcard below.
# ─────────────────────────────────────────────
ALL_CFILES := $(wildcard $(SOURCES)/*.c)

ifeq ($(ENABLE_CITRO2D),1)
  CFILES   := $(ALL_CFILES)
  CFLAGS   += -DCC_ENABLE_CITRO2D -I$(DEVKITPRO)/libctru/include
  LIBS     := -lcitro2d -lcitro3d $(LIBS)
else
  CFILES   := $(filter-out $(SOURCES)/gui_citro2d.c,$(ALL_CFILES))
endif

OBJS     := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))

.PHONY: all clean install

all: $(BUILD) $(TARGET).3gx

$(BUILD):
	@mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c
	@echo "  CC   $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	@echo "  LD   $@"
	@$(LD) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(TARGET).3gx: $(BUILD)/$(TARGET).elf
	@echo "  3GX  $@"
	@$(OBJCOPY) \
	    --strip-debug \
	    --strip-unneeded \
	    -O binary \
	    $< $@
	@echo "  Built: $(TARGET).3gx  (ENABLE_CITRO2D=$(ENABLE_CITRO2D))"
	@ls -lh $(TARGET).3gx

clean:
	@echo "  CLEAN"
	@rm -rf $(BUILD) $(TARGET).3gx $(TARGET).elf

SD_MOUNT ?= /media/$(USER)/SDCARD

install: $(TARGET).3gx
	@echo "  INSTALL → $(SD_MOUNT)/luma/plugins/"
	@mkdir -p "$(SD_MOUNT)/luma/plugins"
	@cp $(TARGET).3gx "$(SD_MOUNT)/luma/plugins/$(TARGET).3gx"
	@echo "  Done.  Eject your SD card and boot the 3DS."
