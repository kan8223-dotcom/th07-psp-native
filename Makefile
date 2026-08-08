export PATH := /usr/local/pspdev/bin:$(PATH)
export PSPDEV := /usr/local/pspdev

TARGET := TH07PSP
PSP_RELEASE_VERSION := v0.1.2-beta
MECC_DIR := psp/third_party/me-custom-core
MECC_BUILD_DIR := $(MECC_DIR)/build
MECC_LIB := $(MECC_BUILD_DIR)/libme-core.a
MECC_INPUTS := $(wildcard $(MECC_DIR)/*.c $(MECC_DIR)/*.h $(MECC_DIR)/*.S \
               $(MECC_DIR)/kernel/*.c $(MECC_DIR)/kernel/*.h $(MECC_DIR)/kernel/*.exp) \
               $(MECC_DIR)/CMakeLists.txt $(MECC_DIR)/kernel/CMakeLists.txt

ENGINE_SRCS := \
    src/AnmManager.cpp \
    src/AnmVm.cpp \
    src/AsciiManager.cpp \
    src/BombData.cpp \
    src/BulletManager.cpp \
    src/Chain.cpp \
    src/Controller.cpp \
    src/EclManager.cpp \
    src/EffectManager.cpp \
    src/Ending.cpp \
    src/EnemyEclInstr.cpp \
    src/EnemyManager.cpp \
    src/FileSystem.cpp \
    src/GameErrorContext.cpp \
    src/GameManager.cpp \
    src/GameWindow.cpp \
    src/Gui.cpp \
    src/ItemManager.cpp \
    src/MainMenu.cpp \
    src/MidiOutput.cpp \
    src/MusicRoom.cpp \
    src/Player.cpp \
    src/ReplayManager.cpp \
    src/ResultScreen.cpp \
    src/Rng.cpp \
    src/ScreenEffect.cpp \
    src/Stage.cpp \
    src/Supervisor.cpp \
    src/TextHelper.cpp \
    src/thirdparty/sjis_converter.cpp \
    src/main.cpp \
    src/pbg4/Lzss.cpp \
    src/pbg4/Pbg4Archive.cpp \
    src/pbg4/Pbg4File.cpp \
    src/utils.cpp

SRCS := psp/platform.cpp psp/fileio.cpp psp/SoundPlayerPsp.cpp psp/audio_me.c \
        psp/graphics/PspGuGraphics.cpp $(ENGINE_SRCS)
OBJS := $(patsubst %.cpp,%.o,$(SRCS))
OBJS := $(patsubst %.c,%.o,$(OBJS))

PSP_EBOOT_TITLE := Touhou 7 PSP Native Beta
PSP_FW_VERSION := 660
BUILD_PRX := 0
EXTRA_TARGETS := EBOOT.PBP

SDL_CFLAGS := $(shell psp-pkg-config --cflags sdl2 SDL2_image SDL2_ttf)

CXXFLAGS := -std=gnu++17 -O2 -G0 -march=allegrex -mtune=allegrex \
            -Wall -Wextra -Wno-unused-parameter -fno-exceptions -fno-rtti -fno-pic -MMD -MP \
            -DPSP -DTH07_PSP -DSDL_MAIN_HANDLED \
            -Ipsp -Isrc -Isrc/pbg4 $(SDL_CFLAGS)
CFLAGS := -O2 -G0 -march=allegrex -mtune=allegrex -Wall -Wextra -fno-pic \
          -Ipsp -Ipsp/third_party/me-custom-core

# Same measured hot translation units as TH06 PSP.  Keep the global build at
# -O2, but allow extra inlining/loop work where sprite, bullet and stage data
# are generated every frame.
src/AnmManager.o src/BulletManager.o src/Player.o src/Stage.o: CXXFLAGS += -O3 -funroll-loops
# These managers walk sparse object pools every frame.  O3 helps the compact
# PSP occupancy-map checks without unrolling their large decompiled bodies.
src/EnemyManager.o src/EffectManager.o src/ItemManager.o: CXXFLAGS += -O3

# The normal menu-driven game is always the default.  The stage route contains
# auto-fire/infinite-lives/MAX-power helpers and is only an explicit test EBOOT.
PSP_DIRECT_GAME ?= 0
PSP_DIRECT_MUSIC ?= 0
PSP_DIRECT_STAGE ?= 3
PSP_DIRECT_TRANSITION_TEST ?= 0
ifeq ($(PSP_DIRECT_GAME),1)
CXXFLAGS += -DTH07_PSP_DIRECT_GAME -DTH07_PSP_DIRECT_STAGE=$(PSP_DIRECT_STAGE)
PSP_EBOOT_TITLE := TH07 PSP stage debug
endif
ifeq ($(PSP_DIRECT_MUSIC),1)
CXXFLAGS += -DTH07_PSP_DIRECT_MUSIC
PSP_EBOOT_TITLE := TH07 PSP music perf
endif
ifeq ($(PSP_DIRECT_TRANSITION_TEST),1)
CXXFLAGS += -DTH07_PSP_DIRECT_TRANSITION_TEST
endif

# Real-hardware profiler.  This keeps the normal title/game route and adds
# low-frequency aggregate GE timing plus transition/heap markers.  It is a
# diagnostic EBOOT and must never be packaged as the normal release.
PSP_PERF_DIAG ?= 0
ifeq ($(PSP_PERF_DIAG),1)
CXXFLAGS += -DTH07_PSP_PERF_DIAG
PSP_EBOOT_TITLE := TH07 PSP perf diag
endif

LIBS := -L$(MECC_BUILD_DIR) -lme-core \
        -lSDL2_image -lSDL2_ttf -lSDL2main -lSDL2 -lGL \
        -lharfbuzz -lfreetype -lbz2 -lpng16 -ljpeg -lz \
        -lpspvram -lpspaudio -lpspvfpu -lpspgum_vfpu -lpspmath -lpspdisplay -lpspgu -lpspge \
        -lpsphprm -lpspctrl -lpsppower -lpthread -latomic -lm -lstdc++ -lsupc++

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

$(MECC_LIB): $(MECC_INPUTS)
	cmake -S $(MECC_DIR) -B $(MECC_BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(MECC_BUILD_DIR) --target me-core

$(TARGET).elf: $(MECC_LIB)

# Changing a Make variable does not normally invalidate existing .o files.
# Keep release and debug objects from ever being silently mixed.
PROFILE_STAMP := .build-profile-$(PSP_DIRECT_GAME)-$(PSP_DIRECT_MUSIC)-$(PSP_PERF_DIAG)-$(PSP_DIRECT_STAGE)-$(PSP_DIRECT_TRANSITION_TEST)
.PHONY: FORCE_PROFILE
$(PROFILE_STAMP): FORCE_PROFILE
	@if [ ! -f "$@" ]; then rm -f .build-profile-*; touch "$@"; fi
$(OBJS): $(PROFILE_STAMP)

.PHONY: release-build release release-audit
release-audit:
	./tools/release_audit.sh

release-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_DIRECT_GAME=0 PSP_PERF_DIAG=0 all
	./tools/release_audit.sh

release: release-build psp/assets/NotoSansJP-Regular.ttf
	@stage_root=$$(mktemp -d); \
	stage="$$stage_root/TH07PSP"; \
	mkdir -p "$$stage/docs" "$$stage/licenses/NotoSansJP" "$$stage/licenses/MECC" dist; \
	cp EBOOT.PBP psp/assets/NotoSansJP-Regular.ttf README.md CREDITS.md CHANGELOG.md LICENSE "$$stage/"; \
	cp docs/KNOWN_ISSUES.md "$$stage/docs/"; \
	cp licenses/NotoSansJP/OFL.txt "$$stage/licenses/NotoSansJP/"; \
	cp psp/third_party/me-custom-core/LICENSE.md "$$stage/licenses/MECC/"; \
	stage_win=$$(wslpath -w "$$stage"); \
	zip_win=$$(wslpath -w "$$stage_root/th07-psp-native-$(PSP_RELEASE_VERSION).zip"); \
	powershell.exe -NoProfile -Command "Compress-Archive -LiteralPath '$$stage_win' -DestinationPath '$$zip_win' -Force"; \
	mv "$$stage_root/th07-psp-native-$(PSP_RELEASE_VERSION).zip" dist/th07-psp-native-$(PSP_RELEASE_VERSION).zip
	./tools/release_audit.sh

# build.mak does not otherwise notice title-only changes.  The profile stamp is
# required too: switching from a perf/direct build to release must regenerate
# PARAM.SFO or the release PBP retains the diagnostic title/marker.
$(PSP_EBOOT_SFO): $(PROFILE_STAMP) Makefile

-include $(OBJS:.o=.d)
