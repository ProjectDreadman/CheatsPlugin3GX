# ══════════════════════════════════════════════════════════════════════════
#  CustomCheats — Luma3DS 3GX Plugin  ·  Makefile
#
#  Requirements:
#    · devkitPro with devkitARM  (https://devkitpro.org)
#    · libctru  (pacman -S 3ds-libctru)
#    · Luma3DS plugin SDK linker script (3gx.ld) installed alongside libctru
#
#  Build:
#    make          → CustomCheats.3gx
#    make clean    → remove build artefacts
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

CFILES   := $(wildcard $(SOURCES)/*.c)
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
	@echo "  Built: $(TARGET).3gx"
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
