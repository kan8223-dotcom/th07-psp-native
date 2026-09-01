export PATH := /usr/local/pspdev/bin:$(PATH)
export PSPDEV := /usr/local/pspdev

TARGET := TH07PSP
PSP_RELEASE_VERSION := v0.1.7-beta
PSP_RELEASE_1000_ZIP := th07-psp-native-$(PSP_RELEASE_VERSION)-psp1000.zip
PSP_RELEASE_2000PLUS_ZIP := th07-psp-native-$(PSP_RELEASE_VERSION)-psp2000plus.zip
PSP_SHIKIGAMI ?= 0
PSP_SHIKIGAMI_HOST_IPV4 ?= 192.168.11.3
PSP_MECC_BGM_384K ?= 0
PSP_MECC_AUDIO_4M ?= 0
PSP_EASY_MIST_AUDIO ?= 0
PSP_BULLET_AXIS_FAST ?= 0
PSP_BULLET_SNAPSHOT_EMITTER ?= 0
PSP_BULLET_ROTATED_DIRECT ?= 0
PSP_BULLET_UNIFIED_QUADS ?= 0
PSP_BULLET_ONEPASS_ROTATED ?= 0
PSP_BULLET_HOT_PREFETCH ?= 0
PSP_BULLET_WARM_QUEUE ?= 0
PSP_BULLET_STATIC_PROXY ?= 0
PSP_ENEMY_P5_WARM_QUEUE ?= 0
PSP_BULLET_QUIESCENT_ANM ?= 0
PSP_ASCII_POPUP_BATCH ?= 0
PSP_GUI_TILE_BATCH ?= 0
PSP_USAGE_METER ?= 0
PSP_USAGE_METER_TOGGLE ?= 0
PSP_FONT_MAIN_RAM ?= 0
PSP_LOCAL_FONT_SUBSET ?= 0
PSP_TITLE_ARCHIVE_WORKSPACE ?= 0
PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT ?= 0
PSP_TITLE_FONT_HOLE_SWAP ?= 0
PSP_FONT_TAIL_ARCHIVE ?= 0
PSP_TEXT_BLIT_FAST ?= 0
PSP_TEXT_PREWARM_PROFILE ?= 0
PSP_PERF_GPU_ATTRIB ?= 0
PSP_PERF_ATTRIB_TARGET ?= M2
PSP_PERF_EMPTY_TIMERS ?= 0
PSP_PERF_DENSE_SLICE ?= 0
PSP_PERF_PLAYER_SHOT ?= 0
PSP_PERF_AB_COMPARE ?= 0
PSP_ME_RENDER_WORKER ?= 0
PSP_ME_RENDER_CORRECTNESS ?= 0
PSP_ME_RENDER_RETIRE_DIAG ?= 0
PSP_ME_RENDER_GE_CONSUME ?= 0
PSP_ME_RENDER_PERFORMANCE ?= 0
PSP_ME_RENDER_RAW_LIVE ?= 0
PSP_ME_RENDER_DIRECT_LIST ?= 0
PSP_ME_BULLET_FAST_UPDATE ?= 0
PSP_ME_BULLET_COMPACT_UPDATE ?= 0
PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY ?= 0
PSP_ME_ITEM_RENDER_STREAM ?= 0
PSP_ME_ITEM_MOTION_UPDATE ?= 0
PSP_ME_EFFECT_RENDER_STREAM ?= 0
PSP_ME_RENDER_UV16 ?= 0
PSP_ME_RENDER_XYZ16 ?= 0
PSP_ME_RENDER_16BIT_GE_EXPERIMENT ?= 0
PSP_ME_BULLET_OUTPUT_SLIM ?= 0
PSP_ME_BULLET_SEED_SLIM ?= 0
PSP_ME_BULLET_SEED_SOA ?= 0
PSP_BULLET_POSITION_SOA_SHADOW ?= 0
PSP_BULLET_POSITION_SOA_READ ?= 0
PSP_ME_ITEM_SEED_SLIM ?= 0
PSP_ME_ADAPTIVE_AUX_RENDER ?= 0
PSP_ME_ITEM_PREFIX_SPLIT ?= 0
PSP_ME_EDRAM_SEED_BENCH ?= 0
PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP ?= 0
PSP_ME_STARTUP_BREADCRUMBS ?= 0
PSP_BULLET_COLLISION_BROADPHASE ?= 0
PSP_AUDIO4M_BUILD_ID ?= 0x2608280bu
MECC_DIR := psp/third_party/me-custom-core
MECC_BUILD_DIR := $(MECC_DIR)/build
MECC_LIB := $(MECC_BUILD_DIR)/libme-core.a
MECC_PROFILE_STAMP := $(MECC_BUILD_DIR)/.th07-render-worker-$(PSP_ME_RENDER_WORKER)
GE4_PROVEN_PRX_SOURCE := ../TH07_GE4_ME4_V6_SIMULTANEOUS_CANARY_20260826/deps/ge-wrapper/ge4wrap_texv1.prx
GE4_PROVEN_PRX := ge4wrap_texv1.prx
GE4_PROVEN_PRX_SIZE := 2150
GE4_PROVEN_PRX_SHA256 := 411e71b3ffb31bd91024cc0221481a787e693276c0899e05da08c3cd91dc1ab8
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

PSP_1000 ?= 0
SRCS := psp/platform.cpp psp/fileio.cpp psp/psp1000_arena.cpp psp/psp1000_title_cache.cpp psp/SoundPlayerPsp.cpp psp/audio_me.c \
        psp/sdl_renderer_stub.cpp psp/graphics/PspGuGraphics.cpp $(ENGINE_SRCS)
ifneq ($(PSP_1000),1)
SRCS += psp/optional_ram_budget.cpp
endif
ifeq ($(PSP_MECC_AUDIO_4M),1)
SRCS += psp/audio4m_sfx.cpp psp/ge4_game_bridge.cpp
endif
ifeq ($(PSP_SHIKIGAMI),1)
SRCS += psp/shikigami_th07.c
endif
ifeq ($(PSP_USAGE_METER),1)
SRCS += psp/usage_meter.c
endif
ifeq ($(PSP_TITLE_FONT_HOLE_SWAP),1)
SRCS += psp/title_font_hole_swap.cpp
endif
OBJS := $(patsubst %.cpp,%.o,$(SRCS))
OBJS := $(patsubst %.c,%.o,$(OBJS))

PSP_EBOOT_TITLE := Touhou 7 PSP-2000+ Beta
PSP_FW_VERSION := 660
BUILD_PRX := 0
EXTRA_TARGETS := EBOOT.PBP
ifeq ($(PSP_MECC_AUDIO_4M),1)
EXTRA_TARGETS += $(GE4_PROVEN_PRX)
endif

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
ifeq ($(PSP_SHIKIGAMI),1)
ifneq ($(PSP_1000),0)
$(error PSP_SHIKIGAMI is a PSP-2000+ diagnostic profile only)
endif
CXXFLAGS += -DTH07_PSP_SHIKIGAMI \
            -DTH07_SHIKIGAMI_HOST_IPV4=\"$(PSP_SHIKIGAMI_HOST_IPV4)\"
CFLAGS += -DTH07_PSP_SHIKIGAMI \
          -DTH07_SHIKIGAMI_HOST_IPV4=\"$(PSP_SHIKIGAMI_HOST_IPV4)\"
PSP_EBOOT_TITLE := Touhou 7 PSP SHIKIGAMI
endif
ifeq ($(PSP_1000),1)
CXXFLAGS += -DTH07_PSP_1000
PSP_EBOOT_TITLE := Touhou 7 PSP-1000 Beta
endif
ifeq ($(PSP_BULLET_AXIS_FAST),1)
CXXFLAGS += -DTH07_PSP_BULLET_AXIS_FAST
endif
ifeq ($(PSP_BULLET_SNAPSHOT_EMITTER),1)
ifneq ($(PSP_1000),0)
$(error PSP_BULLET_SNAPSHOT_EMITTER is a PSP-2000+ validation profile only)
endif
ifneq ($(PSP_BULLET_AXIS_FAST),0)
$(error PSP_BULLET_SNAPSHOT_EMITTER and the rejected PSP_BULLET_AXIS_FAST experiment are mutually exclusive)
endif
ifneq ($(PSP_BULLET_ROTATED_DIRECT),0)
$(error PSP_BULLET_SNAPSHOT_EMITTER and PSP_BULLET_ROTATED_DIRECT are mutually exclusive validation increments)
endif
CXXFLAGS += -DTH07_PSP_BULLET_SNAPSHOT_EMITTER
endif
ifeq ($(PSP_BULLET_ROTATED_DIRECT),1)
ifneq ($(PSP_1000),0)
$(error PSP_BULLET_ROTATED_DIRECT is a PSP-2000+ validation profile only)
endif
ifneq ($(PSP_BULLET_AXIS_FAST),0)
$(error PSP_BULLET_ROTATED_DIRECT and the rejected PSP_BULLET_AXIS_FAST experiment are mutually exclusive)
endif
ifneq ($(PSP_BULLET_SNAPSHOT_EMITTER),0)
$(error PSP_BULLET_ROTATED_DIRECT and PSP_BULLET_SNAPSHOT_EMITTER are mutually exclusive validation increments)
endif
ifeq ($(PSP_PERF_DIAG),1)
ifeq ($(PSP_PERF_PROFILE),ATTRIB)
$(error PSP_BULLET_ROTATED_DIRECT changes the attributed state/corner boundary; use PERF_ACCEPT for A/B)
endif
endif
CXXFLAGS += -DTH07_PSP_BULLET_ROTATED_DIRECT
endif
ifeq ($(PSP_BULLET_UNIFIED_QUADS),1)
ifneq ($(PSP_1000),0)
$(error PSP_BULLET_UNIFIED_QUADS is a PSP-2000+ validation profile only)
endif
ifneq ($(PSP_BULLET_AXIS_FAST),0)
$(error PSP_BULLET_UNIFIED_QUADS and the rejected PSP_BULLET_AXIS_FAST experiment are mutually exclusive)
endif
ifneq ($(PSP_BULLET_SNAPSHOT_EMITTER),0)
$(error PSP_BULLET_UNIFIED_QUADS and PSP_BULLET_SNAPSHOT_EMITTER are mutually exclusive validation increments)
endif
ifeq ($(PSP_PERF_DIAG),1)
ifeq ($(PSP_PERF_PROFILE),ATTRIB)
$(error PSP_BULLET_UNIFIED_QUADS changes the bullet batch boundary; use PERF_ACCEPT for A/B)
endif
endif
CXXFLAGS += -DTH07_PSP_BULLET_UNIFIED_QUADS
endif
ifeq ($(PSP_BULLET_ONEPASS_ROTATED),1)
ifneq ($(PSP_1000),0)
$(error PSP_BULLET_ONEPASS_ROTATED is a PSP-2000+ validation profile only)
endif
ifneq ($(PSP_BULLET_ROTATED_DIRECT),1)
$(error PSP_BULLET_ONEPASS_ROTATED requires PSP_BULLET_ROTATED_DIRECT=1 for exact fallback)
endif
ifneq ($(PSP_BULLET_AXIS_FAST),0)
$(error PSP_BULLET_ONEPASS_ROTATED and the rejected PSP_BULLET_AXIS_FAST experiment are mutually exclusive)
endif
ifneq ($(PSP_BULLET_SNAPSHOT_EMITTER),0)
$(error PSP_BULLET_ONEPASS_ROTATED and PSP_BULLET_SNAPSHOT_EMITTER are mutually exclusive validation increments)
endif
ifeq ($(PSP_PERF_DIAG),1)
ifeq ($(PSP_PERF_PROFILE),ATTRIB)
$(error PSP_BULLET_ONEPASS_ROTATED changes the bullet frontend boundary; use PERF_ACCEPT for A/B)
endif
endif
CXXFLAGS += -DTH07_PSP_BULLET_ONEPASS_ROTATED
endif
ifeq ($(PSP_BULLET_HOT_PREFETCH),1)
ifneq ($(PSP_1000),0)
$(error PSP_BULLET_HOT_PREFETCH is a PSP-2000+ validation profile only)
endif
ifneq ($(PSP_BULLET_ROTATED_DIRECT),1)
$(error PSP_BULLET_HOT_PREFETCH requires the accepted rotated-direct stack)
endif
ifneq ($(PSP_BULLET_UNIFIED_QUADS),1)
$(error PSP_BULLET_HOT_PREFETCH requires the accepted unified-quad stack)
endif
ifneq ($(PSP_BULLET_ONEPASS_ROTATED),1)
$(error PSP_BULLET_HOT_PREFETCH requires the accepted one-pass stack)
endif
CXXFLAGS += -DTH07_PSP_BULLET_HOT_PREFETCH
else ifneq ($(PSP_BULLET_HOT_PREFETCH),0)
$(error PSP_BULLET_HOT_PREFETCH must be 0 or 1)
endif
ifeq ($(PSP_BULLET_WARM_QUEUE),1)
ifneq ($(PSP_1000),0)
$(error PSP_BULLET_WARM_QUEUE is a PSP-2000+ validation profile only)
endif
ifneq ($(PSP_BULLET_ROTATED_DIRECT),1)
$(error PSP_BULLET_WARM_QUEUE requires the accepted rotated-direct stack)
endif
ifneq ($(PSP_BULLET_UNIFIED_QUADS),1)
$(error PSP_BULLET_WARM_QUEUE requires the accepted unified-quad stack)
endif
ifneq ($(PSP_BULLET_ONEPASS_ROTATED),1)
$(error PSP_BULLET_WARM_QUEUE requires the accepted one-pass stack)
endif
ifneq ($(PSP_BULLET_HOT_PREFETCH),0)
$(error PSP_BULLET_WARM_QUEUE and PSP_BULLET_HOT_PREFETCH are mutually exclusive)
endif
ifneq ($(PSP_BULLET_SNAPSHOT_EMITTER),0)
$(error PSP_BULLET_WARM_QUEUE and PSP_BULLET_SNAPSHOT_EMITTER are mutually exclusive)
endif
ifneq ($(PSP_BULLET_QUIESCENT_ANM),0)
$(error PSP_BULLET_WARM_QUEUE and PSP_BULLET_QUIESCENT_ANM are mutually exclusive)
endif
CXXFLAGS += -DTH07_PSP_BULLET_WARM_QUEUE
else ifneq ($(PSP_BULLET_WARM_QUEUE),0)
$(error PSP_BULLET_WARM_QUEUE must be 0 or 1)
endif
ifeq ($(PSP_BULLET_STATIC_PROXY),1)
ifneq ($(PSP_1000),0)
$(error PSP_BULLET_STATIC_PROXY is a PSP-2000+ validation profile only)
endif
ifneq ($(PSP_PERF_DIAG),1)
$(error PSP_BULLET_STATIC_PROXY is PERF_ACCEPT-only)
endif
ifneq ($(PSP_PERF_PROFILE),PERF_ACCEPT)
$(error PSP_BULLET_STATIC_PROXY requires PSP_PERF_PROFILE=PERF_ACCEPT)
endif
ifneq ($(PSP_BULLET_ROTATED_DIRECT),1)
$(error PSP_BULLET_STATIC_PROXY requires the accepted rotated-direct stack)
endif
ifneq ($(PSP_BULLET_UNIFIED_QUADS),1)
$(error PSP_BULLET_STATIC_PROXY requires the accepted unified-quad stack)
endif
ifneq ($(PSP_BULLET_ONEPASS_ROTATED),1)
$(error PSP_BULLET_STATIC_PROXY requires the accepted one-pass stack)
endif
ifneq ($(PSP_BULLET_WARM_QUEUE),0)
$(error PSP_BULLET_STATIC_PROXY and PSP_BULLET_WARM_QUEUE are mutually exclusive)
endif
ifneq ($(PSP_BULLET_SNAPSHOT_EMITTER),0)
$(error PSP_BULLET_STATIC_PROXY and PSP_BULLET_SNAPSHOT_EMITTER are mutually exclusive)
endif
ifneq ($(PSP_BULLET_HOT_PREFETCH),0)
$(error PSP_BULLET_STATIC_PROXY and PSP_BULLET_HOT_PREFETCH are mutually exclusive)
endif
ifneq ($(PSP_ENEMY_P5_WARM_QUEUE),0)
$(error PSP_BULLET_STATIC_PROXY and PSP_ENEMY_P5_WARM_QUEUE are mutually exclusive)
endif
ifneq ($(PSP_BULLET_QUIESCENT_ANM),0)
$(error PSP_BULLET_STATIC_PROXY requires rejected quiescent-ANM OFF)
endif
CXXFLAGS += -DTH07_PSP_BULLET_STATIC_PROXY
else ifneq ($(PSP_BULLET_STATIC_PROXY),0)
$(error PSP_BULLET_STATIC_PROXY must be 0 or 1)
endif
ifeq ($(PSP_ENEMY_P5_WARM_QUEUE),1)
ifneq ($(PSP_1000),0)
$(error PSP_ENEMY_P5_WARM_QUEUE is a PSP-2000+ validation profile only)
endif
ifneq ($(PSP_PERF_DIAG),1)
$(error PSP_ENEMY_P5_WARM_QUEUE is PERF_ACCEPT diagnostic-only)
endif
ifneq ($(PSP_PERF_PROFILE),PERF_ACCEPT)
$(error PSP_ENEMY_P5_WARM_QUEUE requires PSP_PERF_PROFILE=PERF_ACCEPT)
endif
ifneq ($(PSP_PERF_DENSE_SLICE),1)
$(error PSP_ENEMY_P5_WARM_QUEUE requires PSP_PERF_DENSE_SLICE=1)
endif
ifneq ($(PSP_BULLET_ROTATED_DIRECT),1)
$(error PSP_ENEMY_P5_WARM_QUEUE requires the accepted rotated-direct stack)
endif
ifneq ($(PSP_BULLET_UNIFIED_QUADS),1)
$(error PSP_ENEMY_P5_WARM_QUEUE requires the accepted unified-quad stack)
endif
ifneq ($(PSP_BULLET_ONEPASS_ROTATED),1)
$(error PSP_ENEMY_P5_WARM_QUEUE requires the accepted one-pass stack)
endif
ifneq ($(PSP_BULLET_WARM_QUEUE),0)
$(error PSP_ENEMY_P5_WARM_QUEUE and PSP_BULLET_WARM_QUEUE are mutually exclusive)
endif
ifneq ($(PSP_BULLET_HOT_PREFETCH),0)
$(error PSP_ENEMY_P5_WARM_QUEUE and PSP_BULLET_HOT_PREFETCH are mutually exclusive)
endif
ifneq ($(PSP_BULLET_SNAPSHOT_EMITTER),0)
$(error PSP_ENEMY_P5_WARM_QUEUE and PSP_BULLET_SNAPSHOT_EMITTER are mutually exclusive)
endif
ifneq ($(PSP_BULLET_QUIESCENT_ANM),0)
$(error PSP_ENEMY_P5_WARM_QUEUE and PSP_BULLET_QUIESCENT_ANM are mutually exclusive)
endif
CXXFLAGS += -DTH07_PSP_ENEMY_P5_WARM_QUEUE
else ifneq ($(PSP_ENEMY_P5_WARM_QUEUE),0)
$(error PSP_ENEMY_P5_WARM_QUEUE must be 0 or 1)
endif
ifeq ($(PSP_BULLET_QUIESCENT_ANM),1)
ifneq ($(PSP_1000),0)
$(error PSP_BULLET_QUIESCENT_ANM is a PSP-2000+ validation profile only)
endif
ifeq ($(PSP_PERF_DIAG),1)
ifeq ($(PSP_PERF_PROFILE),ATTRIB)
$(error PSP_BULLET_QUIESCENT_ANM changes the bullet calc boundary; use PERF_ACCEPT for A/B)
endif
endif
CXXFLAGS += -DTH07_PSP_BULLET_QUIESCENT_ANM
endif
ifeq ($(PSP_ASCII_POPUP_BATCH),1)
ifneq ($(PSP_1000),0)
$(error PSP_ASCII_POPUP_BATCH is a PSP-2000+ validation profile only)
endif
CXXFLAGS += -DTH07_PSP_ASCII_POPUP_BATCH
endif

# Collapse the 120+ identical HUD border-tile front-end calls into three
# ordered native PSP grid emitters. Vertex order, raster coordinates and the
# final observable VM positions remain identical; only repeated SC state work
# is removed.
ifeq ($(PSP_GUI_TILE_BATCH),1)
ifneq ($(PSP_1000),0)
$(error PSP_GUI_TILE_BATCH is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_GUI_TILE_BATCH
PSP_EBOOT_TITLE := TH07 PSP I-ME8 GUI TILE BATCH
else ifneq ($(PSP_GUI_TILE_BATCH),0)
$(error PSP_GUI_TILE_BATCH must be 0 or 1)
endif
ifeq ($(PSP_FONT_MAIN_RAM),1)
ifneq ($(PSP_1000),0)
$(error PSP_FONT_MAIN_RAM is a PSP-2000+ validation profile only)
endif
CXXFLAGS += -DTH07_PSP_FONT_MAIN_RAM
endif
ifeq ($(PSP_LOCAL_FONT_SUBSET),1)
ifneq ($(PSP_1000),0)
$(error PSP_LOCAL_FONT_SUBSET is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_LOCAL_FONT_SUBSET
else ifneq ($(PSP_LOCAL_FONT_SUBSET),0)
$(error PSP_LOCAL_FONT_SUBSET must be 0 or 1)
endif
ifeq ($(PSP_TITLE_ARCHIVE_WORKSPACE),1)
ifneq ($(PSP_1000),0)
$(error PSP_TITLE_ARCHIVE_WORKSPACE is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_TITLE_ARCHIVE_WORKSPACE
else ifneq ($(PSP_TITLE_ARCHIVE_WORKSPACE),0)
$(error PSP_TITLE_ARCHIVE_WORKSPACE must be 0 or 1)
endif
ifeq ($(PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT),1)
ifneq ($(PSP_1000),0)
$(error PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT is PSP-2000+ only)
endif
ifneq ($(PSP_TITLE_ARCHIVE_WORKSPACE),1)
$(error PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT requires PSP_TITLE_ARCHIVE_WORKSPACE=1)
endif
CXXFLAGS += -DTH07_PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT
else ifneq ($(PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT),0)
$(error PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT must be 0 or 1)
endif
ifeq ($(PSP_TITLE_FONT_HOLE_SWAP),1)
ifneq ($(PSP_1000),0)
$(error PSP_TITLE_FONT_HOLE_SWAP is PSP-2000+ only)
endif
ifneq ($(PSP_FONT_MAIN_RAM),1)
$(error PSP_TITLE_FONT_HOLE_SWAP requires PSP_FONT_MAIN_RAM=1)
endif
ifneq ($(PSP_TITLE_ARCHIVE_WORKSPACE),1)
$(error PSP_TITLE_FONT_HOLE_SWAP requires PSP_TITLE_ARCHIVE_WORKSPACE=1)
endif
ifneq ($(PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT),0)
$(error PSP_TITLE_FONT_HOLE_SWAP and PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT are mutually exclusive)
endif
CXXFLAGS += -DTH07_PSP_TITLE_FONT_HOLE_SWAP
else ifneq ($(PSP_TITLE_FONT_HOLE_SWAP),0)
$(error PSP_TITLE_FONT_HOLE_SWAP must be 0 or 1)
endif
ifeq ($(PSP_FONT_TAIL_ARCHIVE),1)
ifneq ($(PSP_1000),0)
$(error PSP_FONT_TAIL_ARCHIVE is PSP-2000+ only)
endif
ifneq ($(PSP_TITLE_ARCHIVE_WORKSPACE),1)
$(error PSP_FONT_TAIL_ARCHIVE requires PSP_TITLE_ARCHIVE_WORKSPACE=1)
endif
ifneq ($(PSP_TITLE_FONT_HOLE_SWAP),1)
$(error PSP_FONT_TAIL_ARCHIVE requires PSP_TITLE_FONT_HOLE_SWAP=1)
endif
ifneq ($(PSP_LOCAL_FONT_SUBSET),1)
$(error PSP_FONT_TAIL_ARCHIVE requires PSP_LOCAL_FONT_SUBSET=1)
endif
CXXFLAGS += -DTH07_PSP_FONT_TAIL_ARCHIVE
else ifneq ($(PSP_FONT_TAIL_ARCHIVE),0)
$(error PSP_FONT_TAIL_ARCHIVE must be 0 or 1)
endif
ifeq ($(PSP_TEXT_BLIT_FAST),1)
ifneq ($(PSP_1000),0)
$(error PSP_TEXT_BLIT_FAST is a PSP-2000+ validation profile only)
endif
ifneq ($(PSP_PERF_DIAG),1)
$(error PSP_TEXT_BLIT_FAST requires PSP_PERF_DIAG=1)
endif
ifneq ($(PSP_TEXT_PREWARM_PROFILE),1)
$(error PSP_TEXT_BLIT_FAST requires PSP_TEXT_PREWARM_PROFILE=1)
endif
CXXFLAGS += -DTH07_PSP_TEXT_BLIT_FAST
endif
ifeq ($(PSP_MECC_BGM_384K),1)
ifneq ($(PSP_1000),0)
$(error PSP_MECC_BGM_384K is not valid for PSP-1000)
endif
ifneq ($(PSP_SHIKIGAMI),1)
$(error PSP_MECC_BGM_384K requires PSP_SHIKIGAMI=1 for real-hardware ownership telemetry)
endif
ifneq ($(PSP_EASY_MIST_AUDIO),0)
$(error PSP_MECC_BGM_384K and PSP_EASY_MIST_AUDIO are mutually exclusive)
endif
CXXFLAGS += -DTH07_PSP_MECC_BGM_384K
CFLAGS += -DTH07_PSP_MECC_BGM_384K
psp/audio_me.o: CFLAGS += -fstack-usage
PSP_EBOOT_TITLE := TH07 SHIKIGAMI MECC BGM 384K
endif
ifeq ($(PSP_MECC_AUDIO_4M),1)
ifneq ($(PSP_1000),0)
$(error PSP_MECC_AUDIO_4M is not valid for PSP-1000)
endif
ifneq ($(PSP_SHIKIGAMI),1)
$(error PSP_MECC_AUDIO_4M requires PSP_SHIKIGAMI=1 for real-hardware ownership telemetry)
endif
ifneq ($(PSP_MECC_BGM_384K),0)
$(error PSP_MECC_AUDIO_4M and PSP_MECC_BGM_384K are mutually exclusive)
endif
ifneq ($(PSP_EASY_MIST_AUDIO),0)
$(error PSP_MECC_AUDIO_4M and PSP_EASY_MIST_AUDIO are mutually exclusive)
endif
CXXFLAGS += -DTH07_PSP_MECC_AUDIO_4M -DTH07_PSP_GE_PORTRAIT_CACHE \
            -DTH07_PSP_SFX_MAIN_RAM -DTH07_PSP_BGM_MAIN_RAM
CFLAGS += -DTH07_PSP_MECC_AUDIO_4M -DTH07_PSP_GE_PORTRAIT_CACHE \
          -DTH07_PSP_SFX_MAIN_RAM -DTH07_PSP_BGM_MAIN_RAM \
          -DTH07_SHIKIGAMI_BUILD_ID=$(PSP_AUDIO4M_BUILD_ID)
psp/audio_me.o: CFLAGS += -fstack-usage
PSP_EBOOT_TITLE := TH07 SHIKIGAMI MAIN RAM AUDIO GE
endif
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
PSP_PERF_PROFILE ?= ATTRIB
CXXFLAGS += -DTH07_PSP_PERF_DIAG
CFLAGS += -DTH07_PSP_PERF_DIAG
ifeq ($(PSP_PERF_PROFILE),ATTRIB)
ifneq ($(PSP_PERF_EMPTY_TIMERS),0)
$(error PSP_PERF_EMPTY_TIMERS is a PERF_ACCEPT A/A profile, not ATTRIB)
endif
ifneq ($(PSP_BULLET_ROTATED_DIRECT),0)
$(error PSP_BULLET_ROTATED_DIRECT changes the attributed state/corner boundary; use PERF_ACCEPT for A/B)
endif
ifneq ($(PSP_BULLET_UNIFIED_QUADS),0)
$(error PSP_BULLET_UNIFIED_QUADS changes the bullet batch boundary; use PERF_ACCEPT for A/B)
endif
ifneq ($(PSP_BULLET_ONEPASS_ROTATED),0)
$(error PSP_BULLET_ONEPASS_ROTATED changes the bullet frontend boundary; use PERF_ACCEPT for A/B)
endif
ifneq ($(PSP_BULLET_HOT_PREFETCH),0)
$(error PSP_BULLET_HOT_PREFETCH changes the bullet frontend schedule; use PERF_ACCEPT for A/B)
endif
ifneq ($(PSP_BULLET_WARM_QUEUE),0)
$(error PSP_BULLET_WARM_QUEUE changes the bullet calc/draw boundary; use PERF_ACCEPT for A/B)
endif
ifneq ($(PSP_BULLET_STATIC_PROXY),0)
$(error PSP_BULLET_STATIC_PROXY changes the bullet calc/draw boundary; use PERF_ACCEPT for A/B)
endif
ifneq ($(PSP_ENEMY_P5_WARM_QUEUE),0)
$(error PSP_ENEMY_P5_WARM_QUEUE changes the enemy calc/draw boundary; use PERF_ACCEPT for A/B)
endif
ifneq ($(PSP_BULLET_QUIESCENT_ANM),0)
$(error PSP_BULLET_QUIESCENT_ANM changes the bullet calc boundary; use PERF_ACCEPT for A/B)
endif
CXXFLAGS += -DTH07_PSP_PERF_ATTRIB -DTH07_PSP_PERF_DETAIL
ifeq ($(PSP_PERF_ATTRIB_TARGET),M2)
CXXFLAGS += -DTH07_PSP_PERF_M2
else ifeq ($(PSP_PERF_ATTRIB_TARGET),M3)
ifneq ($(PSP_BULLET_AXIS_FAST),0)
$(error M3 attribution requires PSP_BULLET_AXIS_FAST=0; the rejected axis experiment is a separate profile)
endif
ifneq ($(PSP_BULLET_SNAPSHOT_EMITTER),0)
$(error M3 attribution requires PSP_BULLET_SNAPSHOT_EMITTER=0; use PERF_ACCEPT for the I2b A/B)
endif
ifneq ($(PSP_BULLET_ROTATED_DIRECT),0)
$(error M3 attribution requires PSP_BULLET_ROTATED_DIRECT=0; use PERF_ACCEPT for the rotated-direct A/B)
endif
ifneq ($(PSP_BULLET_UNIFIED_QUADS),0)
$(error M3 attribution requires PSP_BULLET_UNIFIED_QUADS=0; use PERF_ACCEPT for the unified-quad A/B)
endif
ifneq ($(PSP_BULLET_ONEPASS_ROTATED),0)
$(error M3 attribution requires PSP_BULLET_ONEPASS_ROTATED=0; use PERF_ACCEPT for the one-pass A/B)
endif
ifneq ($(PSP_BULLET_QUIESCENT_ANM),0)
$(error M3 attribution requires PSP_BULLET_QUIESCENT_ANM=0; use PERF_ACCEPT for the quiescent-ANM A/B)
endif
CXXFLAGS += -DTH07_PSP_PERF_M3
else
$(error PSP_PERF_ATTRIB_TARGET must be M2 or M3 for the ATTRIB profile)
endif
else ifeq ($(PSP_PERF_PROFILE),PERF_ACCEPT)
CXXFLAGS += -DTH07_PSP_PERF_ACCEPT
ifeq ($(PSP_PERF_AB_COMPARE),1)
CXXFLAGS += -DTH07_PSP_PERF_AB_COMPARE
CFLAGS += -DTH07_PSP_PERF_AB_COMPARE
else ifneq ($(PSP_PERF_AB_COMPARE),0)
$(error PSP_PERF_AB_COMPARE must be 0 or 1)
endif
ifeq ($(PSP_PERF_DENSE_SLICE),1)
ifneq ($(PSP_PERF_EMPTY_TIMERS),0)
$(error PSP_PERF_DENSE_SLICE and PSP_PERF_EMPTY_TIMERS are mutually exclusive)
endif
ifneq ($(PSP_BULLET_ROTATED_DIRECT),1)
$(error PSP_PERF_DENSE_SLICE requires the accepted rotated-direct stack)
endif
ifneq ($(PSP_BULLET_UNIFIED_QUADS),1)
$(error PSP_PERF_DENSE_SLICE requires the accepted unified-quad stack)
endif
ifneq ($(PSP_BULLET_ONEPASS_ROTATED),1)
$(error PSP_PERF_DENSE_SLICE requires the accepted one-pass stack)
endif
ifneq ($(PSP_BULLET_QUIESCENT_ANM),0)
$(error PSP_PERF_DENSE_SLICE requires rejected quiescent-ANM OFF)
endif
CXXFLAGS += -DTH07_PSP_PERF_DENSE_SLICE -DTH07_PSP_PERF_DETAIL
else ifneq ($(PSP_PERF_DENSE_SLICE),0)
$(error PSP_PERF_DENSE_SLICE must be 0 or 1)
endif
ifeq ($(PSP_PERF_EMPTY_TIMERS),1)
CXXFLAGS += -DTH07_PSP_PERF_EMPTY_TIMERS -DTH07_PSP_PERF_DETAIL
ifneq ($(PSP_BULLET_ROTATED_DIRECT),0)
$(error Empty-timer A/A calibration requires PSP_BULLET_ROTATED_DIRECT=0)
endif
ifneq ($(PSP_BULLET_UNIFIED_QUADS),0)
$(error Empty-timer A/A calibration requires PSP_BULLET_UNIFIED_QUADS=0)
endif
ifneq ($(PSP_BULLET_ONEPASS_ROTATED),0)
$(error Empty-timer A/A calibration requires PSP_BULLET_ONEPASS_ROTATED=0)
endif
ifneq ($(PSP_BULLET_HOT_PREFETCH),0)
$(error Empty-timer A/A calibration requires PSP_BULLET_HOT_PREFETCH=0)
endif
ifneq ($(PSP_BULLET_WARM_QUEUE),0)
$(error Empty-timer A/A calibration requires PSP_BULLET_WARM_QUEUE=0)
endif
ifneq ($(PSP_BULLET_STATIC_PROXY),0)
$(error Empty-timer A/A calibration requires PSP_BULLET_STATIC_PROXY=0)
endif
ifneq ($(PSP_ENEMY_P5_WARM_QUEUE),0)
$(error Empty-timer A/A calibration requires PSP_ENEMY_P5_WARM_QUEUE=0)
endif
ifneq ($(PSP_BULLET_QUIESCENT_ANM),0)
$(error Empty-timer A/A calibration requires PSP_BULLET_QUIESCENT_ANM=0)
endif
ifeq ($(PSP_PERF_ATTRIB_TARGET),M2)
CXXFLAGS += -DTH07_PSP_PERF_M2
else ifeq ($(PSP_PERF_ATTRIB_TARGET),M3)
ifneq ($(PSP_BULLET_AXIS_FAST),0)
$(error M3 A/A calibration requires PSP_BULLET_AXIS_FAST=0)
endif
ifneq ($(PSP_BULLET_SNAPSHOT_EMITTER),0)
$(error M3 A/A calibration requires PSP_BULLET_SNAPSHOT_EMITTER=0)
endif
CXXFLAGS += -DTH07_PSP_PERF_M3
else
$(error PSP_PERF_ATTRIB_TARGET must be M2 or M3 for an empty-timer A/A profile)
endif
else ifneq ($(PSP_PERF_EMPTY_TIMERS),0)
$(error PSP_PERF_EMPTY_TIMERS must be 0 or 1)
endif
else
$(error PSP_PERF_PROFILE must be ATTRIB or PERF_ACCEPT when PSP_PERF_DIAG=1)
endif
ifeq ($(PSP_PERF_GPU_ATTRIB),1)
ifneq ($(PSP_PERF_PROFILE),ATTRIB)
$(error PSP_PERF_GPU_ATTRIB requires PSP_PERF_PROFILE=ATTRIB)
endif
CXXFLAGS += -DTH07_PSP_PERF_GPU_ATTRIB
endif
PSP_EBOOT_TITLE := TH07 PSP perf diag
else
PSP_PERF_PROFILE ?= RELEASE
ifneq ($(PSP_PERF_PROFILE),RELEASE)
$(error PSP_PERF_PROFILE must be RELEASE when PSP_PERF_DIAG=0)
endif
ifneq ($(PSP_PERF_GPU_ATTRIB),0)
$(error PSP_PERF_GPU_ATTRIB requires PSP_PERF_DIAG=1)
endif
ifneq ($(PSP_PERF_EMPTY_TIMERS),0)
$(error PSP_PERF_EMPTY_TIMERS requires PSP_PERF_DIAG=1)
endif
ifneq ($(PSP_PERF_DENSE_SLICE),0)
$(error PSP_PERF_DENSE_SLICE requires PSP_PERF_DIAG=1)
endif
ifneq ($(PSP_PERF_AB_COMPARE),0)
$(error PSP_PERF_AB_COMPARE requires PSP_PERF_DIAG=1 and PERF_ACCEPT)
endif
endif

# The RID30 ME/SC comparison record is intentionally available only in the
# compact PERF_ACCEPT observer.  Reject an accidental ATTRIB build instead of
# silently compiling a binary that never emits HWFPS/ELUS.
ifeq ($(PSP_PERF_AB_COMPARE),1)
ifneq ($(PSP_PERF_DIAG),1)
$(error PSP_PERF_AB_COMPARE requires PSP_PERF_DIAG=1)
endif
ifneq ($(PSP_PERF_PROFILE),PERF_ACCEPT)
$(error PSP_PERF_AB_COMPARE requires PSP_PERF_PROFILE=PERF_ACCEPT)
endif
else ifneq ($(PSP_PERF_AB_COMPARE),0)
$(error PSP_PERF_AB_COMPARE must be 0 or 1)
endif

# Player-shot frontend attribution is a PSP-2000+ PERF_ACCEPT-only observer.
# It contributes no new renderer work and is never compiled into release or
# PSP-1000 profiles.
ifeq ($(PSP_PERF_PLAYER_SHOT),1)
ifneq ($(PSP_1000),0)
$(error PSP_PERF_PLAYER_SHOT is PSP-2000+-only and is not valid for PSP-1000)
endif
ifneq ($(PSP_PERF_DIAG),1)
$(error PSP_PERF_PLAYER_SHOT requires PSP_PERF_DIAG=1)
endif
ifneq ($(PSP_PERF_PROFILE),PERF_ACCEPT)
$(error PSP_PERF_PLAYER_SHOT requires PSP_PERF_PROFILE=PERF_ACCEPT)
endif
CXXFLAGS += -DTH07_PSP_PERF_PLAYER_SHOT
else ifneq ($(PSP_PERF_PLAYER_SHOT),0)
$(error PSP_PERF_PLAYER_SHOT must be 0 or 1)
endif

# M-ME0 is a PSP-3000-only diagnostic.  It starts the custom ME core, runs
# the boot microbench and submits shadow render jobs, but never consumes ME
# output for drawing.  Keep it tied to the exact accepted DENSE stack so a
# hardware run cannot silently compare different renderer configurations.
ifeq ($(PSP_ME_RENDER_WORKER),1)
ifneq ($(PSP_1000),0)
$(error PSP_ME_RENDER_WORKER is PSP-3000-only and is not valid for PSP-1000)
endif
ifneq ($(PSP_SHIKIGAMI),1)
$(error PSP_ME_RENDER_WORKER requires PSP_SHIKIGAMI=1 for RAM-log telemetry)
endif
ifneq ($(PSP_MECC_AUDIO_4M),1)
$(error PSP_ME_RENDER_WORKER requires the model-3 AUDIO4M/custom-core profile)
endif
ifneq ($(PSP_PERF_DIAG),1)
$(error PSP_ME_RENDER_WORKER requires PSP_PERF_DIAG=1)
endif
ifneq ($(PSP_PERF_PROFILE),PERF_ACCEPT)
$(error PSP_ME_RENDER_WORKER requires PSP_PERF_PROFILE=PERF_ACCEPT)
endif
ifneq ($(PSP_PERF_DENSE_SLICE),1)
$(error PSP_ME_RENDER_WORKER requires the accepted DENSE attribution profile)
endif
ifneq ($(PSP_BULLET_ROTATED_DIRECT),1)
$(error PSP_ME_RENDER_WORKER requires accepted ROTATED_DIRECT=1)
endif
ifneq ($(PSP_BULLET_UNIFIED_QUADS),1)
$(error PSP_ME_RENDER_WORKER requires accepted UNIFIED_QUADS=1)
endif
ifneq ($(PSP_BULLET_ONEPASS_ROTATED),1)
$(error PSP_ME_RENDER_WORKER requires accepted ONEPASS_ROTATED=1)
endif
ifneq ($(PSP_BULLET_AXIS_FAST),0)
$(error PSP_ME_RENDER_WORKER requires rejected AXIS_FAST=0)
endif
ifneq ($(PSP_BULLET_SNAPSHOT_EMITTER),0)
$(error PSP_ME_RENDER_WORKER requires rejected SNAPSHOT_EMITTER=0)
endif
ifneq ($(PSP_BULLET_HOT_PREFETCH),0)
$(error PSP_ME_RENDER_WORKER requires HOT_PREFETCH=0)
endif
ifneq ($(PSP_BULLET_WARM_QUEUE),0)
$(error PSP_ME_RENDER_WORKER requires rejected WARM_QUEUE=0)
endif
ifneq ($(PSP_BULLET_STATIC_PROXY),0)
$(error PSP_ME_RENDER_WORKER requires rejected STATIC_PROXY=0)
endif
ifneq ($(PSP_ENEMY_P5_WARM_QUEUE),0)
$(error PSP_ME_RENDER_WORKER requires rejected ENEMY_P5_WARM_QUEUE=0)
endif
ifneq ($(PSP_BULLET_QUIESCENT_ANM),0)
$(error PSP_ME_RENDER_WORKER requires rejected QUIESCENT_ANM=0)
endif
CXXFLAGS += -DTH07_PSP_ME_RENDER_WORKER
CFLAGS += -DTH07_PSP_ME_RENDER_WORKER
ifeq ($(PSP_ME_RENDER_CORRECTNESS),1)
CXXFLAGS += -DTH07_PSP_ME_RENDER_CORRECTNESS
CFLAGS += -DTH07_PSP_ME_RENDER_CORRECTNESS
ifeq ($(PSP_ME_RENDER_GE_CONSUME),1)
CXXFLAGS += -DTH07_PSP_ME_RENDER_GE_CONSUME
CFLAGS += -DTH07_PSP_ME_RENDER_GE_CONSUME
else ifneq ($(PSP_ME_RENDER_GE_CONSUME),0)
$(error PSP_ME_RENDER_GE_CONSUME must be 0 or 1)
endif
ifeq ($(PSP_ME_RENDER_RETIRE_DIAG),1)
CXXFLAGS += -DTH07_PSP_ME_RENDER_RETIRE_DIAG
CFLAGS += -DTH07_PSP_ME_RENDER_RETIRE_DIAG
ifeq ($(PSP_ME_RENDER_GE_CONSUME),1)
PSP_EBOOT_TITLE := TH07 PSP ME Render I-ME2 GE
else
PSP_EBOOT_TITLE := TH07 PSP ME Render I-ME1 RD
endif
else ifneq ($(PSP_ME_RENDER_RETIRE_DIAG),0)
$(error PSP_ME_RENDER_RETIRE_DIAG must be 0 or 1)
else
ifeq ($(PSP_ME_RENDER_GE_CONSUME),1)
PSP_EBOOT_TITLE := TH07 PSP ME Render I-ME2 GE
else
PSP_EBOOT_TITLE := TH07 PSP ME Render I-ME1
endif
endif
else ifneq ($(PSP_ME_RENDER_CORRECTNESS),0)
$(error PSP_ME_RENDER_CORRECTNESS must be 0 or 1)
else ifneq ($(PSP_ME_RENDER_RETIRE_DIAG),0)
$(error PSP_ME_RENDER_RETIRE_DIAG requires PSP_ME_RENDER_CORRECTNESS=1)
else ifneq ($(PSP_ME_RENDER_GE_CONSUME),0)
$(error PSP_ME_RENDER_GE_CONSUME requires PSP_ME_RENDER_CORRECTNESS=1)
else
PSP_EBOOT_TITLE := TH07 PSP ME Render M0
endif
else ifneq ($(PSP_ME_RENDER_WORKER),0)
$(error PSP_ME_RENDER_WORKER must be 0 or 1)
else ifneq ($(PSP_ME_RENDER_CORRECTNESS),0)
$(error PSP_ME_RENDER_CORRECTNESS requires PSP_ME_RENDER_WORKER=1)
else ifneq ($(PSP_ME_RENDER_RETIRE_DIAG),0)
$(error PSP_ME_RENDER_RETIRE_DIAG requires PSP_ME_RENDER_WORKER=1)
else ifneq ($(PSP_ME_RENDER_GE_CONSUME),0)
$(error PSP_ME_RENDER_GE_CONSUME requires PSP_ME_RENDER_WORKER=1)
endif

# I-ME3's hardware performance profile keeps the I-ME2 GE owner and exact
# READY/run validation, fuses record capture into calc 12, and removes the
# correctness-only stream hashes and per-record draw-deadline reconstruction.
# Keep it explicit so no accepted I-ME1/M0 or diagnostic I-ME2 artifact can
# silently inherit the lighter authority model.
ifeq ($(PSP_ME_RENDER_PERFORMANCE),1)
ifneq ($(PSP_ME_RENDER_WORKER),1)
$(error PSP_ME_RENDER_PERFORMANCE requires PSP_ME_RENDER_WORKER=1)
endif
ifneq ($(PSP_ME_RENDER_CORRECTNESS),1)
$(error PSP_ME_RENDER_PERFORMANCE requires PSP_ME_RENDER_CORRECTNESS=1)
endif
ifneq ($(PSP_ME_RENDER_GE_CONSUME),1)
$(error PSP_ME_RENDER_PERFORMANCE requires PSP_ME_RENDER_GE_CONSUME=1)
endif
ifneq ($(PSP_ME_RENDER_RETIRE_DIAG),0)
$(error PSP_ME_RENDER_PERFORMANCE requires PSP_ME_RENDER_RETIRE_DIAG=0)
endif
CXXFLAGS += -DTH07_PSP_ME_RENDER_PERFORMANCE
CFLAGS += -DTH07_PSP_ME_RENDER_PERFORMANCE
PSP_EBOOT_TITLE := TH07 PSP ME Render I-ME3 PERF
else ifneq ($(PSP_ME_RENDER_PERFORMANCE),0)
$(error PSP_ME_RENDER_PERFORMANCE must be 0 or 1)
endif

# I-ME4 removes I-ME3's SC-side 64-byte semantic record construction.  SC
# publishes a compact, pointer-bearing snapshot after the proven calc-12 VM
# side effects; ME reads the immutable live VM/sprite state and owns the
# remaining geometry, cull, run and vertex work.  Keep this a separate profile
# so the accepted I-ME3 artifact remains reproducible and cannot silently gain
# live-pointer/cache-coherency semantics.
ifeq ($(PSP_ME_RENDER_RAW_LIVE),1)
ifneq ($(PSP_ME_RENDER_PERFORMANCE),1)
$(error PSP_ME_RENDER_RAW_LIVE requires PSP_ME_RENDER_PERFORMANCE=1)
endif
ifneq ($(PSP_1000),0)
$(error PSP_ME_RENDER_RAW_LIVE is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_ME_RENDER_RAW_LIVE
CFLAGS += -DTH07_PSP_ME_RENDER_RAW_LIVE
PSP_EBOOT_TITLE := TH07 PSP ME Render I-ME4 RAW
else ifneq ($(PSP_ME_RENDER_RAW_LIVE),0)
$(error PSP_ME_RENDER_RAW_LIVE must be 0 or 1)
endif

# I-ME5 removes the remaining SC-side per-bullet render record and the
# six-bucket compaction copy.  ME walks the immutable post-calc canonical
# bullet lists directly; I-ME4 remains independently reproducible.
ifeq ($(PSP_ME_RENDER_DIRECT_LIST),1)
ifneq ($(PSP_ME_RENDER_RAW_LIVE),1)
$(error PSP_ME_RENDER_DIRECT_LIST requires PSP_ME_RENDER_RAW_LIVE=1)
endif
ifneq ($(PSP_1000),0)
$(error PSP_ME_RENDER_DIRECT_LIST is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_ME_RENDER_DIRECT_LIST
CFLAGS += -DTH07_PSP_ME_RENDER_DIRECT_LIST
PSP_EBOOT_TITLE := TH07 PSP ME Render I-ME5 LIST
else ifneq ($(PSP_ME_RENDER_DIRECT_LIST),0)
$(error PSP_ME_RENDER_DIRECT_LIST must be 0 or 1)
endif

# I-ME6 keeps the accepted I-ME5 draw worker and additionally sends the pure
# portion of NORMAL-bullet update (motion, bounds and provably-negative
# collision classification) through ME.  Gameplay side effects, collision
# commits, ANM, timers and canonical list publication remain on SC.
ifeq ($(PSP_ME_BULLET_FAST_UPDATE),1)
ifneq ($(PSP_ME_RENDER_DIRECT_LIST),1)
$(error PSP_ME_BULLET_FAST_UPDATE requires PSP_ME_RENDER_DIRECT_LIST=1)
endif
ifneq ($(PSP_1000),0)
$(error PSP_ME_BULLET_FAST_UPDATE is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_ME_BULLET_FAST_UPDATE
CFLAGS += -DTH07_PSP_ME_BULLET_FAST_UPDATE
PSP_EBOOT_TITLE := TH07 PSP ME Render I-ME6 UPDATE
else ifneq ($(PSP_ME_BULLET_FAST_UPDATE),0)
$(error PSP_ME_BULLET_FAST_UPDATE must be 0 or 1)
endif

# I-ME7 replaces I-ME6's synchronous scattered live-pool traversal with a
# compact, double-buffered packet produced as a byproduct of I-ME5's existing
# direct-list walk.  The next frame starts the compact ME job early and never
# waits at the Bullet callback: a late or changed slot stays canonical on SC.
ifeq ($(PSP_ME_BULLET_COMPACT_UPDATE),1)
ifneq ($(PSP_ME_RENDER_DIRECT_LIST),1)
$(error PSP_ME_BULLET_COMPACT_UPDATE requires PSP_ME_RENDER_DIRECT_LIST=1)
endif
ifneq ($(PSP_ME_BULLET_FAST_UPDATE),0)
$(error PSP_ME_BULLET_COMPACT_UPDATE and PSP_ME_BULLET_FAST_UPDATE are mutually exclusive)
endif
ifneq ($(PSP_1000),0)
$(error PSP_ME_BULLET_COMPACT_UPDATE is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_ME_BULLET_COMPACT_UPDATE
CFLAGS += -DTH07_PSP_ME_BULLET_COMPACT_UPDATE
PSP_EBOOT_TITLE := TH07 PSP ME I-ME7 COMPACT
else ifneq ($(PSP_ME_BULLET_COMPACT_UPDATE),0)
$(error PSP_ME_BULLET_COMPACT_UPDATE must be 0 or 1)
endif

# I-ME8 trusts the preceding post-calc compact seed only while its full slot
# generation and manager-wide mutation epoch still match.  This removes the
# repeated scattered Bullet/Player revalidation from the stable NORMAL path;
# every uncertain or externally-mutated frame remains canonical.
ifeq ($(PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY),1)
ifneq ($(PSP_ME_BULLET_COMPACT_UPDATE),1)
$(error PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY requires PSP_ME_BULLET_COMPACT_UPDATE=1)
endif
ifneq ($(PSP_ME_RENDER_PERFORMANCE),1)
$(error PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY requires PSP_ME_RENDER_PERFORMANCE=1)
endif
ifneq ($(PSP_1000),0)
$(error PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY
PSP_EBOOT_TITLE := TH07 PSP ME I-ME8 TRUSTED SEED
else ifneq ($(PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY),0)
$(error PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY must be 0 or 1)
endif

# Optional independent Item draw segment in the existing asynchronous command
# 10 stream.  Failure is segment-local: Item falls back to canonical SC draw
# while the accepted I-ME5 Bullet segment remains usable.
ifeq ($(PSP_ME_ITEM_RENDER_STREAM),1)
ifneq ($(PSP_ME_RENDER_DIRECT_LIST),1)
$(error PSP_ME_ITEM_RENDER_STREAM requires PSP_ME_RENDER_DIRECT_LIST=1)
endif
ifneq ($(PSP_1000),0)
$(error PSP_ME_ITEM_RENDER_STREAM is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_ME_ITEM_RENDER_STREAM
CFLAGS += -DTH07_PSP_ME_ITEM_RENDER_STREAM
PSP_EBOOT_TITLE := TH07 PSP ME I-ME7 SC RELIEF
else ifneq ($(PSP_ME_ITEM_RENDER_STREAM),0)
$(error PSP_ME_ITEM_RENDER_STREAM must be 0 or 1)
endif

# A1-MOVE consumes the exact post-update Item prefix sidecar produced by the
# accepted RID29 command-10 walk.  Only pure motion runs on ME; lifetime,
# collision, rewards, timers, ANM and list authority remain canonical on SC.
ifeq ($(PSP_ME_ITEM_MOTION_UPDATE),1)
ifneq ($(PSP_ME_BULLET_COMPACT_UPDATE),1)
$(error PSP_ME_ITEM_MOTION_UPDATE requires PSP_ME_BULLET_COMPACT_UPDATE=1)
endif
ifneq ($(PSP_ME_ITEM_RENDER_STREAM),1)
$(error PSP_ME_ITEM_MOTION_UPDATE requires PSP_ME_ITEM_RENDER_STREAM=1)
endif
ifneq ($(PSP_ME_ADAPTIVE_AUX_RENDER),1)
$(error PSP_ME_ITEM_MOTION_UPDATE requires PSP_ME_ADAPTIVE_AUX_RENDER=1)
endif
ifneq ($(PSP_ME_ITEM_PREFIX_SPLIT),1)
$(error PSP_ME_ITEM_MOTION_UPDATE requires PSP_ME_ITEM_PREFIX_SPLIT=1)
endif
ifneq ($(PSP_1000),0)
$(error PSP_ME_ITEM_MOTION_UPDATE is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_ME_ITEM_MOTION_UPDATE
CFLAGS += -DTH07_PSP_ME_ITEM_MOTION_UPDATE \
	-fno-builtin-atan2f -fno-builtin-cosf -fno-builtin-sinf
PSP_EBOOT_TITLE := TH07 PSP A1 ITEM MOTION
else ifneq ($(PSP_ME_ITEM_MOTION_UPDATE),0)
$(error PSP_ME_ITEM_MOTION_UPDATE must be 0 or 1)
endif

# C1 research profiles shrink only command-10's native GE stream.  UV and XYZ
# are independent so hardware readback can attribute any raster/depth change.
# No accepted/release target enables either flag.  Direct GE consumption is
# additionally locked behind an explicit experiment flag until framebuffer
# and depth readback prove equality on a real PSP-3000.
ifneq ($(filter-out 0 1,$(PSP_ME_RENDER_UV16)),)
$(error PSP_ME_RENDER_UV16 must be 0 or 1)
endif
ifneq ($(filter-out 0 1,$(PSP_ME_RENDER_XYZ16)),)
$(error PSP_ME_RENDER_XYZ16 must be 0 or 1)
endif
ifneq ($(filter-out 0 1,$(PSP_ME_RENDER_16BIT_GE_EXPERIMENT)),)
$(error PSP_ME_RENDER_16BIT_GE_EXPERIMENT must be 0 or 1)
endif
ifneq ($(filter 1,$(PSP_ME_RENDER_UV16) $(PSP_ME_RENDER_XYZ16)),)
ifneq ($(PSP_ME_RENDER_CORRECTNESS),1)
$(error C1 16-bit vertices require PSP_ME_RENDER_CORRECTNESS=1)
endif
ifneq ($(PSP_ME_RENDER_WORKER),1)
$(error C1 16-bit vertices require PSP_ME_RENDER_WORKER=1)
endif
ifneq ($(PSP_1000),0)
$(error C1 16-bit vertices are PSP-2000+ research only)
endif
ifeq ($(PSP_ME_RENDER_GE_CONSUME),1)
ifneq ($(PSP_ME_RENDER_16BIT_GE_EXPERIMENT),1)
$(error C1 GE consumption requires PSP_ME_RENDER_16BIT_GE_EXPERIMENT=1)
endif
endif
ifeq ($(PSP_ME_RENDER_UV16),1)
CXXFLAGS += -DTH07_PSP_ME_RENDER_UV16
CFLAGS += -DTH07_PSP_ME_RENDER_UV16
endif
ifeq ($(PSP_ME_RENDER_XYZ16),1)
CXXFLAGS += -DTH07_PSP_ME_RENDER_XYZ16
CFLAGS += -DTH07_PSP_ME_RENDER_XYZ16
endif
endif
ifeq ($(PSP_ME_RENDER_16BIT_GE_EXPERIMENT),1)
ifeq ($(filter 1,$(PSP_ME_RENDER_UV16) $(PSP_ME_RENDER_XYZ16)),)
$(error PSP_ME_RENDER_16BIT_GE_EXPERIMENT requires UV16 or XYZ16)
endif
CXXFLAGS += -DTH07_PSP_ME_RENDER_16BIT_GE_EXPERIMENT
CFLAGS += -DTH07_PSP_ME_RENDER_16BIT_GE_EXPERIMENT
endif

# C2 reduces command-12's cache traffic without changing authority or math.
# Each arena is independently selectable so PC and hardware gates can isolate
# regressions.  Accepted RID30 and all C1 builds explicitly keep these off.
ifneq ($(filter-out 0 1,$(PSP_ME_BULLET_OUTPUT_SLIM)),)
$(error PSP_ME_BULLET_OUTPUT_SLIM must be 0 or 1)
endif
ifneq ($(filter-out 0 1,$(PSP_ME_BULLET_SEED_SLIM)),)
$(error PSP_ME_BULLET_SEED_SLIM must be 0 or 1)
endif
ifneq ($(filter-out 0 1,$(PSP_ME_BULLET_SEED_SOA)),)
$(error PSP_ME_BULLET_SEED_SOA must be 0 or 1)
endif
ifneq ($(filter-out 0 1,$(PSP_BULLET_POSITION_SOA_SHADOW)),)
$(error PSP_BULLET_POSITION_SOA_SHADOW must be 0 or 1)
endif
ifneq ($(filter-out 0 1,$(PSP_BULLET_POSITION_SOA_READ)),)
$(error PSP_BULLET_POSITION_SOA_READ must be 0 or 1)
endif
ifneq ($(filter-out 0 1,$(PSP_ME_ITEM_SEED_SLIM)),)
$(error PSP_ME_ITEM_SEED_SLIM must be 0 or 1)
endif

# D1 SoA transposes the exact compact Bullet seed into fourteen raw-u32 field
# planes.  D1A changes layout only and is correctness-only; D1B enables the
# already-reviewed trusted hot reader.  It is never combined with C2b because
# both switches own the same seed ABI.
ifeq ($(PSP_ME_BULLET_SEED_SOA),1)
ifneq ($(PSP_ME_BULLET_COMPACT_UPDATE),1)
$(error D1 SoA requires PSP_ME_BULLET_COMPACT_UPDATE=1)
endif
ifneq ($(PSP_ME_BULLET_SEED_SLIM),0)
$(error D1 SoA and PSP_ME_BULLET_SEED_SLIM are mutually exclusive)
endif
ifneq ($(filter 1,$(PSP_ME_RENDER_UV16) $(PSP_ME_RENDER_XYZ16) $(PSP_ME_RENDER_16BIT_GE_EXPERIMENT) $(PSP_ME_BULLET_OUTPUT_SLIM) $(PSP_ME_ITEM_SEED_SLIM) $(PSP_ME_EFFECT_RENDER_STREAM) $(PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP) $(PSP_ME_EDRAM_SEED_BENCH)),)
$(error D1 SoA isolation requires C1, other C2 arenas, Effect, lean-cache and eDRAM bench OFF)
endif
ifneq ($(PSP_1000),0)
$(error D1 SoA is PSP-2000+ research only)
endif
CXXFLAGS += -DTH07_PSP_ME_BULLET_SEED_SOA
CFLAGS += -DTH07_PSP_ME_BULLET_SEED_SOA
endif

# D2A is a correctness/coverage observer for the eventual persistent Bullet
# position authority.  AoS remains canonical and is still written on every
# path; the sidecar only proves that a full-generation, manager-identity and
# calc-serial fenced SoA representation survives ordinary frames, pause and
# demo calc-chain restart boundaries.  Keep it off PSP-1000 and require the
# exact ME correctness profile whose slot generations it consumes.
ifeq ($(PSP_BULLET_POSITION_SOA_SHADOW),1)
ifneq ($(PSP_ME_BULLET_COMPACT_UPDATE),1)
$(error D2A position SoA shadow requires PSP_ME_BULLET_COMPACT_UPDATE=1)
endif
ifneq ($(PSP_ME_RENDER_CORRECTNESS),1)
$(error D2A position SoA shadow requires PSP_ME_RENDER_CORRECTNESS=1)
endif
ifneq ($(PSP_PERF_AB_COMPARE),1)
$(error D2A position SoA shadow requires PSP_PERF_AB_COMPARE=1)
endif
ifneq ($(PSP_1000),0)
$(error D2A position SoA shadow is PSP-2000+ research only)
endif
CXXFLAGS += -DTH07_PSP_BULLET_POSITION_SOA_SHADOW
endif

# D2B changes readers, not authority: every canonical Bullet::pos write stays
# live and the correctness profile compares every accepted SoA read against
# the AoS raw bits.  It is deliberately isolated from PSP-1000 and cannot be
# enabled without D2A's full-generation/manager/calc fenced shadow.
ifeq ($(PSP_BULLET_POSITION_SOA_READ),1)
ifneq ($(PSP_BULLET_POSITION_SOA_SHADOW),1)
$(error D2B position SoA read requires PSP_BULLET_POSITION_SOA_SHADOW=1)
endif
ifneq ($(PSP_ME_RENDER_DIRECT_LIST),1)
$(error D2B position SoA read requires the versioned ME direct-list owner)
endif
ifneq ($(PSP_PERF_AB_COMPARE),1)
$(error D2B position SoA read requires PSP_PERF_AB_COMPARE=1)
endif
ifneq ($(PSP_1000),0)
$(error D2B position SoA read is PSP-2000+ research only)
endif
CXXFLAGS += -DTH07_PSP_BULLET_POSITION_SOA_READ
CFLAGS += -DTH07_PSP_BULLET_POSITION_SOA_READ
endif
ifneq ($(filter 1,$(PSP_ME_BULLET_OUTPUT_SLIM) $(PSP_ME_BULLET_SEED_SLIM) $(PSP_ME_ITEM_SEED_SLIM)),)
ifneq ($(PSP_ME_BULLET_COMPACT_UPDATE),1)
$(error C2 slim arenas require PSP_ME_BULLET_COMPACT_UPDATE=1)
endif
ifneq ($(PSP_1000),0)
$(error C2 slim arenas are PSP-2000+ research only)
endif
ifeq ($(PSP_ME_BULLET_OUTPUT_SLIM),1)
CXXFLAGS += -DTH07_PSP_ME_BULLET_OUTPUT_SLIM
CFLAGS += -DTH07_PSP_ME_BULLET_OUTPUT_SLIM
endif
ifeq ($(PSP_ME_BULLET_SEED_SLIM),1)
CXXFLAGS += -DTH07_PSP_ME_BULLET_SEED_SLIM
CFLAGS += -DTH07_PSP_ME_BULLET_SEED_SLIM
endif
ifeq ($(PSP_ME_ITEM_SEED_SLIM),1)
ifneq ($(PSP_ME_ITEM_MOTION_UPDATE),1)
$(error C2 Item seed packing requires PSP_ME_ITEM_MOTION_UPDATE=1)
endif
CXXFLAGS += -DTH07_PSP_ME_ITEM_SEED_SLIM
CFLAGS += -DTH07_PSP_ME_ITEM_SEED_SLIM
endif
endif

# I-ME8 extends the already-fenced Item/Bullet stream with Effect layer 0/3.
# Both layers are one fail-closed optional segment; layer 2 remains canonical
# between them, preserving the original priority-9 order exactly.
ifeq ($(PSP_ME_EFFECT_RENDER_STREAM),1)
ifneq ($(PSP_ME_ITEM_RENDER_STREAM),1)
$(error PSP_ME_EFFECT_RENDER_STREAM requires PSP_ME_ITEM_RENDER_STREAM=1)
endif
ifneq ($(PSP_ME_RENDER_PERFORMANCE),1)
$(error PSP_ME_EFFECT_RENDER_STREAM requires PSP_ME_RENDER_PERFORMANCE=1)
endif
ifneq ($(PSP_1000),0)
$(error PSP_ME_EFFECT_RENDER_STREAM is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_ME_EFFECT_RENDER_STREAM
CFLAGS += -DTH07_PSP_ME_EFFECT_RENDER_STREAM
PSP_EBOOT_TITLE := TH07 PSP ME I-ME8 EFFECT
else ifneq ($(PSP_ME_EFFECT_RENDER_STREAM),0)
$(error PSP_ME_EFFECT_RENDER_STREAM must be 0 or 1)
endif

# Optional Item/Effect draw expansion is admitted from a deterministic
# record-count cost model.  The one-frame-delayed usage meter is only a
# fail-closed veto; it is never the positive admission signal.
#
# RID23 keeps all-or-none admission. RID24 enables the reviewed IL02 bounded
# split: totalCount+suffixHead are authenticated after ME invalidation and at
# SC consume, then ME prefix is issued before the canonical SC suffix.
ifeq ($(PSP_ME_ADAPTIVE_AUX_RENDER),1)
ifneq ($(PSP_ME_ITEM_RENDER_STREAM),1)
$(error PSP_ME_ADAPTIVE_AUX_RENDER requires PSP_ME_ITEM_RENDER_STREAM=1)
endif
ifneq ($(PSP_ME_RENDER_PERFORMANCE),1)
$(error PSP_ME_ADAPTIVE_AUX_RENDER requires PSP_ME_RENDER_PERFORMANCE=1)
endif
ifneq ($(PSP_ME_RENDER_GE_CONSUME),1)
$(error PSP_ME_ADAPTIVE_AUX_RENDER requires PSP_ME_RENDER_GE_CONSUME=1)
endif
ifneq ($(PSP_USAGE_METER),1)
$(error PSP_ME_ADAPTIVE_AUX_RENDER requires PSP_USAGE_METER=1)
endif
ifneq ($(PSP_1000),0)
$(error PSP_ME_ADAPTIVE_AUX_RENDER is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_ME_ADAPTIVE_AUX_RENDER
CFLAGS += -DTH07_PSP_ME_ADAPTIVE_AUX_RENDER
else ifneq ($(PSP_ME_ADAPTIVE_AUX_RENDER),0)
$(error PSP_ME_ADAPTIVE_AUX_RENDER must be 0 or 1)
endif
ifeq ($(PSP_ME_ITEM_PREFIX_SPLIT),1)
ifneq ($(PSP_ME_ADAPTIVE_AUX_RENDER),1)
$(error PSP_ME_ITEM_PREFIX_SPLIT requires PSP_ME_ADAPTIVE_AUX_RENDER=1)
endif
CXXFLAGS += -DTH07_PSP_ME_ITEM_PREFIX_SPLIT
CFLAGS += -DTH07_PSP_ME_ITEM_PREFIX_SPLIT
else ifneq ($(PSP_ME_ITEM_PREFIX_SPLIT),0)
$(error PSP_ME_ITEM_PREFIX_SPLIT must be 0 or 1)
endif

# M0E is a boot-only A/B measurement of the existing I-ME7 compact seed in
# upper ME-local eDRAM.  It adds no gameplay owner or public API: command 13
# is reachable only from the ME17 startup selftest, and performance NO-GO is
# reported without changing the accepted Main-RAM runtime.
ifeq ($(PSP_ME_EDRAM_SEED_BENCH),1)
ifneq ($(PSP_ME_BULLET_COMPACT_UPDATE),1)
$(error PSP_ME_EDRAM_SEED_BENCH requires PSP_ME_BULLET_COMPACT_UPDATE=1)
endif
ifneq ($(PSP_ME_RENDER_DIRECT_LIST),1)
$(error PSP_ME_EDRAM_SEED_BENCH requires PSP_ME_RENDER_DIRECT_LIST=1)
endif
ifneq ($(PSP_MECC_AUDIO_4M),1)
$(error PSP_ME_EDRAM_SEED_BENCH requires PSP_MECC_AUDIO_4M=1)
endif
ifneq ($(PSP_1000),0)
$(error PSP_ME_EDRAM_SEED_BENCH is PSP-3000-only)
endif
ifneq ($(PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY),0)
$(error PSP_ME_EDRAM_SEED_BENCH requires TRUSTED_SEED_AUTHORITY=0)
endif
ifneq ($(PSP_ME_ITEM_RENDER_STREAM),0)
$(error PSP_ME_EDRAM_SEED_BENCH requires ITEM_RENDER_STREAM=0)
endif
ifneq ($(PSP_ME_EFFECT_RENDER_STREAM),0)
$(error PSP_ME_EDRAM_SEED_BENCH requires EFFECT_RENDER_STREAM=0)
endif
ifneq ($(PSP_ME_ADAPTIVE_AUX_RENDER),0)
$(error PSP_ME_EDRAM_SEED_BENCH requires ADAPTIVE_AUX_RENDER=0)
endif
ifneq ($(PSP_ME_ITEM_PREFIX_SPLIT),0)
$(error PSP_ME_EDRAM_SEED_BENCH requires ITEM_PREFIX_SPLIT=0)
endif
CFLAGS += -DTH07_PSP_ME_EDRAM_SEED_BENCH
PSP_EBOOT_TITLE := TH07 PSP ME EDRAM SEED BENCH
else ifneq ($(PSP_ME_EDRAM_SEED_BENCH),0)
$(error PSP_ME_EDRAM_SEED_BENCH must be 0 or 1)
endif

# Hardware-rejected experiment.  I-ME8R stopped during the command-10
# direct-list startup selftest when these full cache fences were omitted.
# Keep the variable in the profile stamp so archived artifacts remain
# identifiable, but refuse to produce another unsafe EBOOT.
ifeq ($(PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP),1)
$(error PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP is hardware-rejected; use 0)
else ifneq ($(PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP),0)
$(error PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP must be 0 or 1)
endif

# Startup-only recovery markers for the hardware bisect profiles.  These add
# no gameplay timer and are deliberately absent from ordinary/release builds.
ifeq ($(PSP_ME_STARTUP_BREADCRUMBS),1)
ifneq ($(PSP_ME_RENDER_CORRECTNESS),1)
$(error PSP_ME_STARTUP_BREADCRUMBS requires PSP_ME_RENDER_CORRECTNESS=1)
endif
ifneq ($(PSP_1000),0)
$(error PSP_ME_STARTUP_BREADCRUMBS is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_ME_STARTUP_BREADCRUMBS
CFLAGS += -DTH07_PSP_ME_STARTUP_BREADCRUMBS
else ifneq ($(PSP_ME_STARTUP_BREADCRUMBS),0)
$(error PSP_ME_STARTUP_BREADCRUMBS must be 0 or 1)
endif

# Warm-SC fallback for frames where the compact ME command yields to audio or
# is not ready.  It only rejects collision when the canonical graze and
# killbox AABBs are both provably disjoint and no bomb volume is active.
ifeq ($(PSP_BULLET_COLLISION_BROADPHASE),1)
ifneq ($(PSP_1000),0)
$(error PSP_BULLET_COLLISION_BROADPHASE is PSP-2000+ only)
endif
CXXFLAGS += -DTH07_PSP_BULLET_COLLISION_BROADPHASE
PSP_EBOOT_TITLE := TH07 PSP ME I-ME7 SC RELIEF
else ifneq ($(PSP_BULLET_COLLISION_BROADPHASE),0)
$(error PSP_BULLET_COLLISION_BROADPHASE must be 0 or 1)
endif

# Aggregate-only timing for the stage text pre-render path.  This profile is
# intentionally tied to a diagnostic EBOOT: release builds pay no timer or log
# cost, and DIRECT_GAME's per-row boot notes must not contaminate the sample.
ifeq ($(PSP_TEXT_PREWARM_PROFILE),1)
ifneq ($(PSP_1000),0)
$(error PSP_TEXT_PREWARM_PROFILE is a PSP-2000+ diagnostic profile only)
endif
ifneq ($(PSP_PERF_DIAG),1)
$(error PSP_TEXT_PREWARM_PROFILE requires PSP_PERF_DIAG=1)
endif
ifneq ($(PSP_DIRECT_GAME),0)
$(error PSP_TEXT_PREWARM_PROFILE requires PSP_DIRECT_GAME=0)
endif
CXXFLAGS += -DTH07_PSP_TEXT_PREWARM_PROFILE
endif

LIBS := -L$(MECC_BUILD_DIR) -lme-core \
        -lSDL2_image -lSDL2_ttf -lSDL2main -lSDL2 -lGL \
        -lharfbuzz -lfreetype -lbz2 -lpng16 -ljpeg -lz \
        -lpspvram -lpspaudio -lpspvfpu -lpspgum_vfpu -lpspmath -lpspdisplay -lpspgu -lpspge \
        -lpsphprm -lpspctrl -lpsppower -lpthread -latomic -lm -lstdc++ -lsupc++
ifeq ($(PSP_SHIKIGAMI),1)
# PSPSDK's GCC specs place psputility/pspnet_inet after libcglue, and
# build.mak appends pspnet_apctl.  Listing those archives here too can split
# one module's import stubs and make psp-fixup-imports report them out of
# order.  The R6 GE bridge uses kubridge plus systemctrl's user export lookup;
# it deliberately links no import stub for the kernel wrapper itself.
LIBS += -lpspkubridge
ifeq ($(PSP_MECC_AUDIO_4M),1)
LIBS += -lpspsystemctrl_user
endif
endif

PSPSDK := $(shell psp-config --pspsdk-path)
# [FABLE] SC/ME usage meter (I-ME7 companion overlay, display-only)
ifeq ($(PSP_USAGE_METER),1)
CXXFLAGS += -DTH07_PSP_USAGE_METER
CFLAGS += -DTH07_PSP_USAGE_METER
PSP_EBOOT_TITLE := TH07 PSP I-ME7 USAGE METER
else ifneq ($(PSP_USAGE_METER),0)
$(error PSP_USAGE_METER must be 0 or 1)
endif
ifeq ($(PSP_USAGE_METER_TOGGLE),1)
ifneq ($(PSP_USAGE_METER),1)
$(error PSP_USAGE_METER_TOGGLE requires PSP_USAGE_METER=1)
endif
CFLAGS += -DTH07_PSP_USAGE_METER_TOGGLE
else ifneq ($(PSP_USAGE_METER_TOGGLE),0)
$(error PSP_USAGE_METER_TOGGLE must be 0 or 1)
endif

include $(PSPSDK)/lib/build.mak

.PHONY: FORCE_GE4_PROVEN_PRX
FORCE_GE4_PROVEN_PRX:

$(GE4_PROVEN_PRX): FORCE_GE4_PROVEN_PRX $(GE4_PROVEN_PRX_SOURCE)
	@test "$$(wc -c < "$(GE4_PROVEN_PRX_SOURCE)")" -eq "$(GE4_PROVEN_PRX_SIZE)"
	@test "$$(sha256sum "$(GE4_PROVEN_PRX_SOURCE)" | awk '{print $$1}')" = "$(GE4_PROVEN_PRX_SHA256)"
	cp "$(GE4_PROVEN_PRX_SOURCE)" "$@"
	@test "$$(wc -c < "$@")" -eq "$(GE4_PROVEN_PRX_SIZE)"
	@test "$$(sha256sum "$@" | awk '{print $$1}')" = "$(GE4_PROVEN_PRX_SHA256)"

.PHONY: ge4-proven-clean
ge4-proven-clean:
	rm -f psp/ge4_game_bridge.o psp/ge4_game_bridge.d \
		psp/audio4m_sfx.o psp/audio4m_sfx.d psp/audio_me.su \
		$(GE4_PROVEN_PRX)
clean: ge4-proven-clean

$(MECC_PROFILE_STAMP):
	cmake -S $(MECC_DIR) -B $(MECC_BUILD_DIR) -DCMAKE_BUILD_TYPE=Release \
		-DTH07_ME_RENDER_WORKER=$(if $(filter 1,$(PSP_ME_RENDER_WORKER)),ON,OFF)
	rm -f $(MECC_BUILD_DIR)/.th07-render-worker-*
	touch $@

$(MECC_LIB): $(MECC_INPUTS) $(MECC_PROFILE_STAMP)
	cmake --build $(MECC_BUILD_DIR) --target me-core

$(TARGET).elf: $(MECC_LIB)

ifneq ($(filter 1,$(PSP_MECC_BGM_384K) $(PSP_MECC_AUDIO_4M)),)
.PHONY: mecc-proven-audit
mecc-proven-audit: $(MECC_LIB)
	$(if $(filter 1,$(PSP_ME_RENDER_WORKER)),python3 tools/audit_mecc_proven.py $(MECC_LIB) $(MECC_BUILD_DIR)/kernel/kcall.prx --render-worker-candidate,python3 tools/audit_mecc_proven.py $(MECC_LIB) $(MECC_BUILD_DIR)/kernel/kcall.prx)
$(TARGET).elf: | mecc-proven-audit
endif

# Changing a Make variable does not normally invalidate existing .o files.
# Keep release/debug objects and observer destination addresses from ever
# being silently mixed.
SHIKIGAMI_HOST_STAMP := $(subst .,_,$(PSP_SHIKIGAMI_HOST_IPV4))
PROFILE_STAMP := .build-profile-$(PSP_DIRECT_GAME)-$(PSP_DIRECT_MUSIC)-$(PSP_PERF_DIAG)-$(PSP_PERF_PROFILE)-$(PSP_PERF_ATTRIB_TARGET)-$(PSP_PERF_GPU_ATTRIB)-$(PSP_PERF_EMPTY_TIMERS)-$(PSP_PERF_DENSE_SLICE)-$(PSP_PERF_PLAYER_SHOT)-$(PSP_PERF_AB_COMPARE)-$(PSP_ME_RENDER_WORKER)-$(PSP_ME_RENDER_CORRECTNESS)-$(PSP_ME_RENDER_RETIRE_DIAG)-$(PSP_ME_RENDER_GE_CONSUME)-$(PSP_ME_RENDER_PERFORMANCE)-$(PSP_ME_RENDER_RAW_LIVE)-$(PSP_ME_RENDER_DIRECT_LIST)-$(PSP_ME_BULLET_FAST_UPDATE)-$(PSP_ME_BULLET_COMPACT_UPDATE)-$(PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)-$(PSP_ME_ITEM_RENDER_STREAM)-$(PSP_ME_ITEM_MOTION_UPDATE)-$(PSP_ME_EFFECT_RENDER_STREAM)-$(PSP_ME_RENDER_UV16)-$(PSP_ME_RENDER_XYZ16)-$(PSP_ME_RENDER_16BIT_GE_EXPERIMENT)-$(PSP_ME_BULLET_OUTPUT_SLIM)-$(PSP_ME_BULLET_SEED_SLIM)-$(PSP_ME_BULLET_SEED_SOA)-$(PSP_BULLET_POSITION_SOA_SHADOW)-$(PSP_BULLET_POSITION_SOA_READ)-$(PSP_ME_ITEM_SEED_SLIM)-$(PSP_ME_ADAPTIVE_AUX_RENDER)-$(PSP_ME_ITEM_PREFIX_SPLIT)-$(PSP_ME_EDRAM_SEED_BENCH)-$(PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP)-$(PSP_ME_STARTUP_BREADCRUMBS)-$(PSP_BULLET_COLLISION_BROADPHASE)-$(PSP_DIRECT_STAGE)-$(PSP_DIRECT_TRANSITION_TEST)-$(PSP_1000)-$(PSP_SHIKIGAMI)-$(PSP_MECC_BGM_384K)-$(PSP_MECC_AUDIO_4M)-$(PSP_BULLET_AXIS_FAST)-$(PSP_BULLET_SNAPSHOT_EMITTER)-$(PSP_BULLET_ROTATED_DIRECT)-$(PSP_BULLET_UNIFIED_QUADS)-$(PSP_BULLET_ONEPASS_ROTATED)-$(PSP_BULLET_HOT_PREFETCH)-$(PSP_BULLET_WARM_QUEUE)-$(PSP_BULLET_STATIC_PROXY)-$(PSP_ENEMY_P5_WARM_QUEUE)-$(PSP_BULLET_QUIESCENT_ANM)-$(PSP_ASCII_POPUP_BATCH)-$(PSP_GUI_TILE_BATCH)-$(PSP_FONT_MAIN_RAM)-$(PSP_LOCAL_FONT_SUBSET)-$(PSP_TITLE_ARCHIVE_WORKSPACE)-$(PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT)-$(PSP_TITLE_FONT_HOLE_SWAP)-$(PSP_FONT_TAIL_ARCHIVE)-$(PSP_TEXT_BLIT_FAST)-$(PSP_TEXT_PREWARM_PROFILE)-$(PSP_USAGE_METER)-$(PSP_USAGE_METER_TOGGLE)-$(PSP_AUDIO4M_BUILD_ID)-$(SHIKIGAMI_HOST_STAMP)
.PHONY: FORCE_PROFILE
$(PROFILE_STAMP): FORCE_PROFILE
	@if [ ! -f "$@" ]; then rm -f .build-profile-*; touch "$@"; fi
$(OBJS): $(PROFILE_STAMP)

.PHONY: psp1000-build psp2000plus-build psp2000plus-shikigami-build \
	psp3000-mecc-bgm384k-build psp3000-mecc-audio4m-build psp3000-me-render-m0-build \
	psp3000-me-render-i1-build \
	psp3000-me-render-i1-retire-diag-build \
	psp3000-me-render-i2-ge-build \
	psp3000-me-render-i3-performance-build \
	psp3000-me-render-i4-raw-build \
	psp3000-me-render-i5-direct-list-build \
	psp3000-me-render-i6-bullet-fast-build \
	psp3000-me-render-i7-sc-relief-build \
	psp3000-me-render-i8-allin-build \
	psp3000-me-render-i8r-no-effect-build \
	psp3000-me-render-i8r2-no-effect-no-lean-build \
	psp3000-me-render-i8r3-cache-safe-build \
	psp3000-ime7-edram-seed-bench-build \
	psp3000-rid30-ab-me-build psp3000-rid30-ab-sc-build \
	psp3000-rid30-ab-me-c1-uv16-m0-build \
	psp3000-rid30-ab-me-d1s0-trusted-build \
	psp3000-rid30-ab-me-d1a-soa-build \
	psp3000-rid30-ab-me-d1b-soa-build \
	psp3000-a6v4w-me-d1s0-trusted-build \
	psp3000-a6v4w-me-d1a-soa-build \
	psp3000-a6v4w-me-d1b-soa-build \
	psp3000-rid30-a6-title-workspace-build \
	psp3000-rid30-a6v4-local-font-subset-build \
	psp3000-rid30-a6v4-cp932-wave-dash-build \
	psp3000-a6v4w-music-room-fontfix-build \
	psp3000-a6v4w-stage6-font-tail-fix-build \
	psp3000-a6v4w-d2a-position-soa-shadow-build \
	psp3000-a6v4w-d2b-position-soa-read-build \
	psp3000-c1-vertex16-m0-build psp3000-c1-uv16-m0-build \
	psp3000-c1-xyz16-m0-build psp3000-c1-uvxyz16-m0-build \
	c2a_output_slim c2b_bullet_seed_slim c2c_item_seed_slim \
	c2abc_all_slim psp3000-c2-slim-build psp3000-player-shot-perf-build \
	release-build release-psp1000 \
	release-psp2000plus release release-audit
psp1000-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=1 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=0 \
		PSP_PERF_PROFILE=RELEASE PSP_PERF_GPU_ATTRIB=0 PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=0 PSP_ME_RENDER_WORKER=0 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=0 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=0 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 PSP_BULLET_ROTATED_DIRECT=0 PSP_BULLET_UNIFIED_QUADS=0 PSP_BULLET_ONEPASS_ROTATED=0 PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=0 PSP_FONT_MAIN_RAM=0 PSP_TEXT_BLIT_FAST=0 PSP_TEXT_PREWARM_PROFILE=0 \
		PSP_EASY_MIST_AUDIO=0 all

psp2000plus-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=0 \
		PSP_PERF_PROFILE=RELEASE PSP_PERF_GPU_ATTRIB=0 PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=0 PSP_ME_RENDER_WORKER=0 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=0 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=0 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 PSP_BULLET_ROTATED_DIRECT=0 PSP_BULLET_UNIFIED_QUADS=0 PSP_BULLET_ONEPASS_ROTATED=0 PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=0 PSP_FONT_MAIN_RAM=0 PSP_TEXT_BLIT_FAST=0 PSP_TEXT_PREWARM_PROFILE=0 \
		PSP_EASY_MIST_AUDIO=0 all

psp2000plus-shikigami-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=0 \
		PSP_PERF_PROFILE=RELEASE PSP_PERF_GPU_ATTRIB=0 PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=0 PSP_ME_RENDER_WORKER=0 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=0 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 PSP_BULLET_ROTATED_DIRECT=0 PSP_BULLET_UNIFIED_QUADS=0 PSP_BULLET_ONEPASS_ROTATED=0 PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=0 PSP_FONT_MAIN_RAM=0 PSP_TEXT_BLIT_FAST=0 PSP_TEXT_PREWARM_PROFILE=0 \
		PSP_EASY_MIST_AUDIO=0 all

psp3000-mecc-bgm384k-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=0 \
		PSP_PERF_PROFILE=RELEASE PSP_PERF_GPU_ATTRIB=0 PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=0 PSP_ME_RENDER_WORKER=0 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=1 \
		PSP_MECC_AUDIO_4M=0 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 PSP_BULLET_ROTATED_DIRECT=0 PSP_BULLET_UNIFIED_QUADS=0 PSP_BULLET_ONEPASS_ROTATED=0 PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=0 PSP_FONT_MAIN_RAM=0 PSP_TEXT_BLIT_FAST=0 PSP_TEXT_PREWARM_PROFILE=0 \
		PSP_EASY_MIST_AUDIO=0 all

psp3000-mecc-audio4m-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=0 \
		PSP_PERF_PROFILE=RELEASE PSP_PERF_GPU_ATTRIB=0 PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=0 PSP_ME_RENDER_WORKER=0 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 PSP_BULLET_ROTATED_DIRECT=0 PSP_BULLET_UNIFIED_QUADS=0 PSP_BULLET_ONEPASS_ROTATED=0 PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=0 PSP_FONT_MAIN_RAM=0 PSP_TEXT_BLIT_FAST=0 PSP_TEXT_PREWARM_PROFILE=0 \
		PSP_EASY_MIST_AUDIO=0 all

psp3000-me-render-m0-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 PSP_PERF_PLAYER_SHOT=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=0 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=0 PSP_ME_RENDER_PERFORMANCE=0 PSP_ME_RENDER_RAW_LIVE=0 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x2608300du PSP_EASY_MIST_AUDIO=0 all

psp3000-me-render-i1-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 PSP_PERF_PLAYER_SHOT=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=0 PSP_ME_RENDER_PERFORMANCE=0 PSP_ME_RENDER_RAW_LIVE=0 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x2608300eu PSP_EASY_MIST_AUDIO=0 all

psp3000-me-render-i1-retire-diag-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 PSP_PERF_PLAYER_SHOT=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=1 PSP_ME_RENDER_GE_CONSUME=0 PSP_ME_RENDER_PERFORMANCE=0 PSP_ME_RENDER_RAW_LIVE=0 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x2608300fu PSP_EASY_MIST_AUDIO=0 all

psp3000-me-render-i2-ge-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 PSP_PERF_PLAYER_SHOT=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=1 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=0 PSP_ME_RENDER_RAW_LIVE=0 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x26083010u PSP_EASY_MIST_AUDIO=0 all

psp3000-me-render-i3-performance-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=0 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x26083013u PSP_EASY_MIST_AUDIO=0 all

psp3000-me-render-i4-raw-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x26083014u PSP_EASY_MIST_AUDIO=0 all

psp3000-me-render-i5-direct-list-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x26083015u PSP_EASY_MIST_AUDIO=0 all

psp3000-me-render-i6-bullet-fast-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x26083016u PSP_EASY_MIST_AUDIO=0 all

psp3000-me-render-i7-sc-relief-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x26083117u PSP_EASY_MIST_AUDIO=0 all

psp3000-me-render-i8-allin-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=1 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_EFFECT_RENDER_STREAM=1 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=1 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x26083118u PSP_EBOOT_TITLE='TH07 PSP ME I-ME8 ALL-IN' \
		PSP_EASY_MIST_AUDIO=0 all

# Startup-recovery profile after the hardware-only I-ME8 ME19 stop.  Keep all
# independently safe I-ME8 SC-relief work, but compile the Effect ABI and
# worker branch completely out so command-10 retains its hardware-proven I-ME7
# shape.  This is a distinct RID and must never be reported as ALL-IN.
psp3000-me-render-i8r-no-effect-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=1 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=1 PSP_ME_STARTUP_BREADCRUMBS=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=1 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x26083119u PSP_EBOOT_TITLE='TH07 PSP ME I-ME8R NO-EFFECT' \
		PSP_EASY_MIST_AUDIO=0 all

# Second startup-recovery profile.  The I-ME8R hardware log proved generic
# and raw command-10 phases, then stopped inside direct-list/Item while lean
# cache ownership was active.  Restore I-ME7 cache publication exactly while
# retaining the independent trusted-seed and GUI SC-relief work.
psp3000-me-render-i8r2-no-effect-no-lean-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=1 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=1 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x2608311au PSP_EBOOT_TITLE='TH07 PSP ME I-ME8R2 SAFE-DIRECT' \
		PSP_EASY_MIST_AUDIO=0 all

# Shipping-safe recovery after the I-ME8R hardware stop.  This is a distinct
# RID from the pre-fix R2 archive: Lean is compile-time rejected globally and
# command-10 always executes the proven I-ME7 full-pool/GE cache fences.
psp3000-me-render-i8r3-cache-safe-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=1 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=1 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_AUDIO4M_BUILD_ID=0x2608311bu PSP_EBOOT_TITLE='TH07 PSP ME I-ME8R3 CACHE-SAFE' \
		PSP_EASY_MIST_AUDIO=0 all

release-audit:
	./tools/release_audit.sh

release-build: psp2000plus-build

release-psp2000plus: psp/assets/NotoSansJP-Regular.ttf
	$(MAKE) psp2000plus-build
	@eboot_hash=$$(sha256sum EBOOT.PBP | awk '{print $$1}'); \
	stage_root=$$(mktemp -d); \
	stage="$$stage_root/TH07PSP"; \
	mkdir -p "$$stage/docs" "$$stage/licenses/NotoSansJP" "$$stage/licenses/MECC" dist; \
	cp EBOOT.PBP psp/assets/NotoSansJP-Regular.ttf README.md README_EN.md CREDITS.md CHANGELOG.md LICENSE "$$stage/"; \
	cp ark/ARK5_HIGHMEM_SNIPPET.txt "$$stage/"; \
	cp docs/KNOWN_ISSUES.md docs/ARK5_HIGH_MEMORY.md "$$stage/docs/"; \
	cp licenses/NotoSansJP/OFL.txt "$$stage/licenses/NotoSansJP/"; \
	cp psp/third_party/me-custom-core/LICENSE.md "$$stage/licenses/MECC/"; \
	stage_win=$$(wslpath -w "$$stage"); \
	zip_win=$$(wslpath -w "$$stage_root/$(PSP_RELEASE_2000PLUS_ZIP)"); \
	powershell.exe -NoProfile -Command "Compress-Archive -LiteralPath '$$stage_win' -DestinationPath '$$zip_win' -Force"; \
	mv "$$stage_root/$(PSP_RELEASE_2000PLUS_ZIP)" "dist/$(PSP_RELEASE_2000PLUS_ZIP)"; \
	printf '%s  %s\n' "$$eboot_hash" EBOOT.PBP > "dist/$(PSP_RELEASE_2000PLUS_ZIP).EBOOT.sha256"
	./tools/release_audit.sh

release-psp1000: psp/assets/NotoSansJP-Regular.ttf
	$(MAKE) psp1000-build
	@eboot_hash=$$(sha256sum EBOOT.PBP | awk '{print $$1}'); \
	stage_root=$$(mktemp -d); \
	stage="$$stage_root/TH07PSP"; \
	mkdir -p "$$stage/docs" "$$stage/licenses/NotoSansJP" "$$stage/licenses/MECC" dist; \
	cp EBOOT.PBP psp/assets/NotoSansJP-Regular.ttf README.md README_EN.md CREDITS.md CHANGELOG.md LICENSE "$$stage/"; \
	cp docs/KNOWN_ISSUES.md "$$stage/docs/"; \
	cp licenses/NotoSansJP/OFL.txt "$$stage/licenses/NotoSansJP/"; \
	cp psp/third_party/me-custom-core/LICENSE.md "$$stage/licenses/MECC/"; \
	stage_win=$$(wslpath -w "$$stage"); \
	zip_win=$$(wslpath -w "$$stage_root/$(PSP_RELEASE_1000_ZIP)"); \
	powershell.exe -NoProfile -Command "Compress-Archive -LiteralPath '$$stage_win' -DestinationPath '$$zip_win' -Force"; \
	mv "$$stage_root/$(PSP_RELEASE_1000_ZIP)" "dist/$(PSP_RELEASE_1000_ZIP)"; \
	printf '%s  %s\n' "$$eboot_hash" EBOOT.PBP > "dist/$(PSP_RELEASE_1000_ZIP).EBOOT.sha256"
	./tools/release_audit.sh

release:
	rm -f "dist/$(PSP_RELEASE_1000_ZIP)" "dist/$(PSP_RELEASE_2000PLUS_ZIP)" \
		"dist/$(PSP_RELEASE_1000_ZIP).EBOOT.sha256" \
		"dist/$(PSP_RELEASE_2000PLUS_ZIP).EBOOT.sha256"
	$(MAKE) release-psp2000plus
	$(MAKE) release-psp1000
	test -f "dist/$(PSP_RELEASE_1000_ZIP)"
	test -f "dist/$(PSP_RELEASE_2000PLUS_ZIP)"
	./tools/release_audit.sh

# build.mak does not otherwise notice title-only changes.  The profile stamp is
# required too: switching from a perf/direct build to release must regenerate
# PARAM.SFO or the release PBP retains the diagnostic title/marker.
$(PSP_EBOOT_SFO): $(PROFILE_STAMP) Makefile

-include $(OBJS:.o=.d)

# [FABLE] I-ME7 + usage meter overlay, dedicated RID 0x26083120
psp3000-ime7-usage-meter-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x26083120u PSP_EASY_MIST_AUDIO=0 all

# Hardware-safe usage-meter profile.  The post-I-ME7 shared Item/Bullet
# command-10 implementation is quarantined after real hardware stopped in its
# Item startup selftest (the last durable marker was DIRECT LIST PASS).  Keep
# the accepted compact Bullet worker and SC broadphase, but compile Item ME,
# Trusted Seed, Effect, Lean cache ownership and GUI batching out.  The only
# new runtime feature is the display-only usage meter.
psp3000-ime7-usage-meter-no-item-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=0 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=0 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x26083121u \
		PSP_EBOOT_TITLE='TH07 PSP I-ME7 METER NO-ITEM' PSP_EASY_MIST_AUDIO=0 all

# Same hardware-safe I-ME7/no-Item runtime as RID21, with the usage meter's
# real ME busy-time source repaired: initialize CP0 Count on the ME and feed
# compact/render invalidate+kernel+writeback cycles into the graph.
psp3000-ime7-usage-meter-mefix-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=0 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=0 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x26083122u \
		PSP_EBOOT_TITLE='TH07 PSP I-ME7 METER MEFIX' PSP_EASY_MIST_AUDIO=0 all

# RID25/M0E: exact RID22 gameplay stack plus a boot-only command-13 A/B of
# the I-ME7 compact seed in short-lived upper ME eDRAM.  No eDRAM pointer or
# decision escapes the startup selftest into gameplay.
psp3000-ime7-edram-seed-bench-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=0 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_ADAPTIVE_AUX_RENDER=0 PSP_ME_ITEM_PREFIX_SPLIT=0 PSP_ME_EDRAM_SEED_BENCH=1 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=0 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x26083125u \
		PSP_EBOOT_TITLE='TH07 PSP ME EDRAM SEED BENCH' PSP_EASY_MIST_AUDIO=0 all

# RID23: keep RID22's accepted Bullet render/compact stack and add only the
# deterministic adaptive Item draw prefix.  Effect remains off for the first
# hardware increment.  The lagging ME meter can veto, never admit, Item work.
psp3000-ime7-adaptive-item-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_ADAPTIVE_AUX_RENDER=1 PSP_ME_ITEM_PREFIX_SPLIT=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=0 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x26083123u \
		PSP_EBOOT_TITLE='TH07 PSP I-ME7 ADAPTIVE ITEM' PSP_EASY_MIST_AUDIO=0 all

# RID24: IL02 cooperative Item owner. ME emits the largest deterministic
# budget-fitting canonical prefix; SC appends only the authenticated suffix.
psp3000-ime7-adaptive-item-prefix-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_ADAPTIVE_AUX_RENDER=1 PSP_ME_ITEM_PREFIX_SPLIT=1 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=0 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x26083124u \
		PSP_EBOOT_TITLE='TH07 PSP I-ME7 ITEM PREFIX IL02' PSP_EASY_MIST_AUDIO=0 all

# RID26: boot-only diagnosis of RID24's fail-closed ME1A startup result.  Keep
# RID24's performance/raw-live/direct-list code path comparable; startup
# breadcrumbs are the only profile change.  This is not a performance claim
# and remains fail-closed.
psp3000-ime7-item-selftest-diag-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_ADAPTIVE_AUX_RENDER=1 PSP_ME_ITEM_PREFIX_SPLIT=1 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x26083126u \
		PSP_EBOOT_TITLE='TH07 PSP ME1A ITEM SELFTEST DIAG' PSP_EASY_MIST_AUDIO=0 all

# RID27 REJECTED ON HARDWARE (2026-08-31): retained only to reserve/archive
# its feature identity.  The source-level index+8192 experiment was reverted,
# so a current-tree rebuild is not a reproduction.  Never deploy this target.
psp3000-ime7-adaptive-item-cachefix-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_ADAPTIVE_AUX_RENDER=1 PSP_ME_ITEM_PREFIX_SPLIT=1 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x26083127u \
		PSP_EBOOT_TITLE='TH07 PSP I-ME7 ITEM CACHEFIX' PSP_EASY_MIST_AUDIO=0 all

# RID28 PC candidate only: replace the disproved guessed index operation with
# Sony T2's native whole-cache handoff for Item jobs.  If Item's isolated
# startup proof still fails cleanly, permanently close Item admission, reset
# the stream slots and re-prove the accepted RID22 Bullet worker so the game
# remains playable instead of cold-rebooting to XMB.
psp3000-ime7-adaptive-item-safe-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 PSP_PERF_PLAYER_SHOT=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0 PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_ADAPTIVE_AUX_RENDER=1 PSP_ME_ITEM_PREFIX_SPLIT=1 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x26083128u \
		PSP_EBOOT_TITLE='TH07 PSP I-ME7 ITEM SAFE' PSP_EASY_MIST_AUDIO=0 all

# RID29 PC candidate: keep RID28's safe fallback, but read every mutable Item
# input through ME's uncached volatile alias.  Immutable sprite metadata keeps
# the accepted cached path.  SHIKIGAMI type 12 reports the complete Item/Bullet
# startup decision without requiring a USB BOOT.LOG recovery.
psp3000-ime7-adaptive-item-uncached-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 PSP_PERF_PLAYER_SHOT=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0 PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_ADAPTIVE_AUX_RENDER=1 PSP_ME_ITEM_PREFIX_SPLIT=1 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x26083129u \
		PSP_EBOOT_TITLE='TH07 PSP I-ME7 ITEM UNCACHED' PSP_EASY_MIST_AUDIO=0 all

# RID30 A1-MOVE candidate.  The only delta from RID29 is the optional Item
# motion sidecar of command 12.  Startup or runtime segment rejection disables
# motion only; RID29 Item draw and the accepted Bullet worker stay active.
psp3000-a1-item-motion-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 PSP_PERF_PLAYER_SHOT=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_ITEM_MOTION_UPDATE=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0 PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_ADAPTIVE_AUX_RENDER=1 PSP_ME_ITEM_PREFIX_SPLIT=1 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x26083130u \
		PSP_EBOOT_TITLE='TH07 PSP A1 ITEM MOTION' PSP_EASY_MIST_AUDIO=0 all

# Fair real-hardware A/B pair for the current RID30 feature set.  Both builds
# use the same compact ACCEPT observer and write the same one-line record per
# 120 PSP frames.  HWFPS is derived exclusively from the PSP system clock;
# ELUS and vblank MISS are retained so the offline tool can verify it without
# consulting replay/SHIKIGAMI FPS fields.
PSP_RID30_AB_ME_UV16 ?= 0
PSP_RID30_AB_ME_XYZ16 ?= 0
PSP_RID30_AB_ME_C1_GE_EXPERIMENT ?= 0
PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY ?= 0
PSP_RID30_AB_ME_SEED_SOA ?= 0
PSP_RID30_AB_ME_POSITION_SOA_SHADOW ?= 0
PSP_RID30_AB_ME_POSITION_SOA_READ ?= 0
PSP_RID30_AB_ME_TITLE_WORKSPACE ?= 0
PSP_RID30_AB_ME_TITLE_TRANSIENT ?= 0
PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP ?= 0
PSP_RID30_AB_ME_LOCAL_FONT_SUBSET ?= 0
PSP_RID30_AB_ME_FONT_TAIL_ARCHIVE ?= 0
PSP_RID30_AB_ME_BUILD_ID ?= 0x260901a1u
PSP_RID30_AB_ME_TITLE ?= TH07 RID30 AB ME

psp3000-rid30-ab-me-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_DIRECT_STAGE=3 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_AB_COMPARE=1 PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 PSP_PERF_PLAYER_SHOT=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=$(PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY) PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_ITEM_MOTION_UPDATE=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_UV16=$(PSP_RID30_AB_ME_UV16) PSP_ME_RENDER_XYZ16=$(PSP_RID30_AB_ME_XYZ16) PSP_ME_RENDER_16BIT_GE_EXPERIMENT=$(PSP_RID30_AB_ME_C1_GE_EXPERIMENT) PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_BULLET_SEED_SOA=$(PSP_RID30_AB_ME_SEED_SOA) PSP_BULLET_POSITION_SOA_SHADOW=$(PSP_RID30_AB_ME_POSITION_SOA_SHADOW) PSP_BULLET_POSITION_SOA_READ=$(PSP_RID30_AB_ME_POSITION_SOA_READ) PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_ADAPTIVE_AUX_RENDER=1 PSP_ME_ITEM_PREFIX_SPLIT=1 PSP_ME_EDRAM_SEED_BENCH=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_SHIKIGAMI_HOST_IPV4= PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_LOCAL_FONT_SUBSET=$(PSP_RID30_AB_ME_LOCAL_FONT_SUBSET) PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=$(PSP_RID30_AB_ME_BUILD_ID) \
		PSP_TITLE_ARCHIVE_WORKSPACE=$(PSP_RID30_AB_ME_TITLE_WORKSPACE) \
		PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT=$(PSP_RID30_AB_ME_TITLE_TRANSIENT) \
		PSP_TITLE_FONT_HOLE_SWAP=$(PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP) \
		PSP_FONT_TAIL_ARCHIVE=$(PSP_RID30_AB_ME_FONT_TAIL_ARCHIVE) \
		PSP_EBOOT_TITLE='$(PSP_RID30_AB_ME_TITLE)' PSP_EASY_MIST_AUDIO=0 all

# Current accepted ME A/B build plus exactly the C1 UV16 ABI/readback gate.
# Keep the compact PSP HWFPS/ELUS observer and empty SHIKIGAMI host so a
# successful M0 boot can advance without changing the performance baseline.
psp3000-rid30-ab-me-c1-uv16-m0-build: PSP_RID30_AB_ME_UV16=1
psp3000-rid30-ab-me-c1-uv16-m0-build: PSP_RID30_AB_ME_XYZ16=0
psp3000-rid30-ab-me-c1-uv16-m0-build: PSP_RID30_AB_ME_C1_GE_EXPERIMENT=1
psp3000-rid30-ab-me-c1-uv16-m0-build: PSP_RID30_AB_ME_BUILD_ID=0x260901c7u
psp3000-rid30-ab-me-c1-uv16-m0-build: PSP_RID30_AB_ME_TITLE=TH07 RID30 AB ME C1 UV16 M0K
psp3000-rid30-ab-me-c1-uv16-m0-build: psp3000-rid30-ab-me-build

# D1 staged SoA matrix.  D1S0 first isolates the already-reviewed trusted
# reader on the frozen BS11 AoS layout.  D1A changes only the seed ABI and is a
# correctness/hash build; do not draw performance conclusions from it.  D1B is
# the first performance candidate and combines the proven trusted reader with
# the plane-transposed BS13 seed.  None of these targets deploys an EBOOT.
psp3000-rid30-ab-me-d1s0-trusted-build:
	@# Contract: PSP_1000=0 PSP_ME_BULLET_SEED_SOA=0 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=1 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0 PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_EFFECT_RENDER_STREAM=0
	$(MAKE) PSP_RID30_AB_ME_UV16=0 PSP_RID30_AB_ME_XYZ16=0 \
		PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=1 \
		PSP_RID30_AB_ME_SEED_SOA=0 PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901d0u \
		PSP_RID30_AB_ME_TITLE='TH07 RID30 D1S0 TRUSTED AOS' \
		psp3000-rid30-ab-me-build

psp3000-rid30-ab-me-d1a-soa-build:
	@# Contract: PSP_1000=0 PSP_ME_BULLET_SEED_SOA=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0 PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_EFFECT_RENDER_STREAM=0
	$(MAKE) PSP_RID30_AB_ME_UV16=0 PSP_RID30_AB_ME_XYZ16=0 \
		PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0 \
		PSP_RID30_AB_ME_SEED_SOA=1 PSP_RID30_AB_ME_TITLE_WORKSPACE=0 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901d1u \
		PSP_RID30_AB_ME_TITLE='TH07 RID30 D1A SOA SHADOW' \
		psp3000-rid30-ab-me-build

psp3000-rid30-ab-me-d1b-soa-build:
	@# Contract: PSP_1000=0 PSP_ME_BULLET_SEED_SOA=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=1 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0 PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_EFFECT_RENDER_STREAM=0
	$(MAKE) PSP_RID30_AB_ME_UV16=0 PSP_RID30_AB_ME_XYZ16=0 \
		PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=1 \
		PSP_RID30_AB_ME_SEED_SOA=1 PSP_RID30_AB_ME_TITLE_WORKSPACE=0 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901d2u \
		PSP_RID30_AB_ME_TITLE='TH07 RID30 D1B SOA TRUSTED' \
		psp3000-rid30-ab-me-build

# Current D1 queue: retain the exact A6v4W title/font path that passed on
# hardware. The RID30 D1 targets above are historical artifacts only. Each
# stage pins every unrelated C1/C2/Effect/eDRAM/lean experiment off; these
# targets build locally but never deploy an EBOOT.
psp3000-a6v4w-me-d1s0-trusted-build:
	@# Contract: PSP_1000=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_SEED_SOA=0 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=1 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0 PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_EDRAM_SEED_BENCH=0 PSP_TITLE_ARCHIVE_WORKSPACE=1 PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT=0 PSP_TITLE_FONT_HOLE_SWAP=1 PSP_LOCAL_FONT_SUBSET=1
	$(MAKE) PSP_1000=0 PSP_RID30_AB_ME_UV16=0 \
		PSP_RID30_AB_ME_XYZ16=0 PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=1 \
		PSP_RID30_AB_ME_SEED_SOA=0 PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=0 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1 \
		PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901f0u \
		PSP_RID30_AB_ME_TITLE='TH07 A6V4W D1S0 TRUSTED AOS' \
		psp3000-rid30-ab-me-build

psp3000-a6v4w-me-d1a-soa-build:
	@# Contract: PSP_1000=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_SEED_SOA=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0 PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_EDRAM_SEED_BENCH=0 PSP_TITLE_ARCHIVE_WORKSPACE=1 PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT=0 PSP_TITLE_FONT_HOLE_SWAP=1 PSP_LOCAL_FONT_SUBSET=1
	$(MAKE) PSP_1000=0 PSP_RID30_AB_ME_UV16=0 \
		PSP_RID30_AB_ME_XYZ16=0 PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0 \
		PSP_RID30_AB_ME_SEED_SOA=1 PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=0 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1 \
		PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901f1u \
		PSP_RID30_AB_ME_TITLE='TH07 A6V4W D1A SOA SHADOW' \
		psp3000-rid30-ab-me-build

psp3000-a6v4w-me-d1b-soa-build:
	@# Contract: PSP_1000=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_SEED_SOA=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=1 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0 PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_EDRAM_SEED_BENCH=0 PSP_TITLE_ARCHIVE_WORKSPACE=1 PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT=0 PSP_TITLE_FONT_HOLE_SWAP=1 PSP_LOCAL_FONT_SUBSET=1
	$(MAKE) PSP_1000=0 PSP_RID30_AB_ME_UV16=0 \
		PSP_RID30_AB_ME_XYZ16=0 PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=1 \
		PSP_RID30_AB_ME_SEED_SOA=1 PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=0 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1 \
		PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901f2u \
		PSP_RID30_AB_ME_TITLE='TH07 A6V4W D1B SOA TRUSTED' \
		psp3000-rid30-ab-me-build

# A6 is orthogonal to D1: retain title01.anm's first successful 5.4 MiB
# allocation so title returns never ask a fragmented heap for another large
# contiguous block.  Keep it as a one-delta RID30 candidate until the stage-5
# return and multi-demo soak both pass on hardware.
psp3000-rid30-a6-title-workspace-build:
	$(MAKE) PSP_RID30_AB_ME_UV16=0 PSP_RID30_AB_ME_XYZ16=0 \
		PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0 \
		PSP_RID30_AB_ME_SEED_SOA=0 PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=0 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=0 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901a6u \
		PSP_RID30_AB_ME_TITLE='TH07 RID30 A6 TITLE WORKSPACE' \
		psp3000-rid30-ab-me-build

# A6v2 retains A6's title-return guarantee while lending the whole workspace
# to one immediate-release face_*.anm source at a time. Gui loads all face
# archives before PrepareStage lends the 1536 KiB prefix to text, so the loans
# never overlap. A non-compact face is rejected instead of publishing a
# lifetime pointer into scratch storage.
psp3000-rid30-a6v2-title-transient-workspace-build:
	$(MAKE) PSP_RID30_AB_ME_UV16=0 PSP_RID30_AB_ME_XYZ16=0 \
		PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0 \
		PSP_RID30_AB_ME_SEED_SOA=0 PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=1 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=0 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901a7u \
		PSP_RID30_AB_ME_TITLE='TH07 RID30 A6V2 FULL LOAN' \
		psp3000-rid30-ab-me-build

# A6v3 is an alternative anti-fragmentation experiment on the frozen RID30
# baseline. FONT and TITLE exchange one 5.4 MiB process arena, so the usual
# 4.4 MiB RAM font and A6 workspace are never held as separate allocations.
# Failed loads keep the TITLE-sized hole open through trim/retry; the successful
# attempt restores the FONT lease. The A6v2 transient face route stays off.
psp3000-rid30-a6v3-title-font-hole-swap-build:
	$(MAKE) PSP_RID30_AB_ME_UV16=0 PSP_RID30_AB_ME_XYZ16=0 \
		PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0 \
		PSP_RID30_AB_ME_SEED_SOA=0 PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=0 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901a8u \
		PSP_RID30_AB_ME_TITLE='TH07 RID30 A6V3 FONT HOLE' \
		psp3000-rid30-ab-me-build

# A6v4 keeps A6v3's proven FONT/TITLE arena ownership and adds only the
# user-local MS Gothic subset gate. The proprietary font never enters this
# repository: the EBOOT accepts msgothic-subset.ttf only when every stock TH07
# codepoint pinned in source is present, otherwise it uses the established
# full-font candidates unchanged.
psp3000-rid30-a6v4-local-font-subset-build:
	$(MAKE) PSP_RID30_AB_ME_UV16=0 PSP_RID30_AB_ME_XYZ16=0 \
		PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0 \
		PSP_RID30_AB_ME_SEED_SOA=0 PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=0 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1 \
		PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901a9u \
		PSP_RID30_AB_ME_TITLE='TH07 RID30 A6V4 FONT SUBSET' \
		psp3000-rid30-ab-me-build

# A6v4.1 keeps the exact 1,190-glyph subset and maps the stock CP932 0x8160
# byte pair to Windows' U+FF5E, matching the original game and MS Gothic.
psp3000-rid30-a6v4-cp932-wave-dash-build:
	$(MAKE) PSP_RID30_AB_ME_UV16=0 PSP_RID30_AB_ME_XYZ16=0 \
		PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0 \
		PSP_RID30_AB_ME_SEED_SOA=0 PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=0 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1 \
		PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901aau \
		PSP_RID30_AB_ME_TITLE='TH07 RID30 A6V4W CP932 WAVE' \
		psp3000-rid30-ab-me-build

# One-delta successor to the hardware-accepted A6v4W profile.  Only the
# resettable SDL_ttf point-size owner fix differs; the unique build id keeps
# its logs distinguishable from the accepted comparison build.
psp3000-a6v4w-music-room-fontfix-build:
	$(MAKE) PSP_RID30_AB_ME_UV16=0 PSP_RID30_AB_ME_XYZ16=0 \
		PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0 \
		PSP_RID30_AB_ME_SEED_SOA=0 PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=0 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1 \
		PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901abu \
		PSP_RID30_AB_ME_TITLE='TH07 A6V4W MUSIC FONT FIX' \
		psp3000-rid30-ab-me-build

# Hardware log MUSICFONT-ST5-XMB proved that the stage-6 face source could not
# obtain a contiguous 3 MiB block from a 5.4 MiB fragmented heap. Keep the
# 293 KiB subset font at the shared-arena head and lend only its disjoint tail
# to serial face_*.anm decode; all gameplay/ME/render flags remain frozen.
psp3000-a6v4w-stage6-font-tail-fix-build:
	$(MAKE) PSP_1000=0 PSP_RID30_AB_ME_UV16=0 \
		PSP_RID30_AB_ME_XYZ16=0 PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0 \
		PSP_RID30_AB_ME_SEED_SOA=0 \
		PSP_RID30_AB_ME_POSITION_SOA_SHADOW=0 \
		PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=0 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1 \
		PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1 \
		PSP_RID30_AB_ME_FONT_TAIL_ARCHIVE=1 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901adu \
		PSP_RID30_AB_ME_TITLE='TH07 A6V4W ST6 TAIL FIX' \
		psp3000-rid30-ab-me-build

# D2A starts the meaningful Bullet SoA cutover without changing authority.
# It keeps the current A6v4W/Music Room fixes, writes the canonical AoS exactly
# as before, and shadows only persistent XYZ/generation/identity planes.  The
# target is PC-only until its coverage and mismatch counters justify D2B.
psp3000-a6v4w-d2a-position-soa-shadow-build:
	$(MAKE) PSP_1000=0 PSP_RID30_AB_ME_UV16=0 \
		PSP_RID30_AB_ME_XYZ16=0 PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0 \
		PSP_RID30_AB_ME_SEED_SOA=0 \
		PSP_RID30_AB_ME_POSITION_SOA_SHADOW=1 \
		PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=0 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1 \
		PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1 \
		PSP_RID30_AB_ME_FONT_TAIL_ARCHIVE=1 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901acu \
		PSP_RID30_AB_ME_TITLE='TH07 A6V4W D2A POS SOA SHADOW' \
		psp3000-rid30-ab-me-build

# D2B keeps every AoS position write, but switches eligible post-calc SC and
# ME direct-list readers through the phase-fenced BP21 planes. Every accepted
# read is raw-bit compared with AoS in this correctness candidate; no D2B
# timing claim is valid until that comparison is removed in a later gate.
psp3000-a6v4w-d2b-position-soa-read-build:
	$(MAKE) PSP_1000=0 PSP_RID30_AB_ME_UV16=0 \
		PSP_RID30_AB_ME_XYZ16=0 PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0 \
		PSP_RID30_AB_ME_SEED_SOA=0 \
		PSP_RID30_AB_ME_POSITION_SOA_SHADOW=1 \
		PSP_RID30_AB_ME_POSITION_SOA_READ=1 \
		PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=0 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1 \
		PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1 \
		PSP_RID30_AB_ME_FONT_TAIL_ARCHIVE=1 \
		PSP_RID30_AB_ME_BUILD_ID=0x260901aeu \
		PSP_RID30_AB_ME_TITLE='TH07 A6V4W D2B POS SOA READ' \
		psp3000-rid30-ab-me-build

# SC member of the pair.  All custom-ME work and its dependent collision
# broadphase are disabled together; accepted RID30 renderer/text/audio/GE
# paths stay identical.  AUDIO4M remains enabled for the shared Main-RAM
# audio and GE portrait architecture, but no custom ME worker is started.
psp3000-rid30-ab-sc-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_AB_COMPARE=1 PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 PSP_PERF_PLAYER_SHOT=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=0 PSP_ME_RENDER_CORRECTNESS=0 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=0 PSP_ME_RENDER_PERFORMANCE=0 PSP_ME_RENDER_RAW_LIVE=0 PSP_ME_RENDER_DIRECT_LIST=0 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=0 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=0 PSP_ME_ITEM_MOTION_UPDATE=0 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0 PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_ADAPTIVE_AUX_RENDER=0 PSP_ME_ITEM_PREFIX_SPLIT=0 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=0 PSP_BULLET_COLLISION_BROADPHASE=0 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_SHIKIGAMI_HOST_IPV4= PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x260901a2u \
		PSP_EBOOT_TITLE='TH07 RID30 AB SC' PSP_EASY_MIST_AUDIO=0 all

# PC-only A4/D2 observer based on frozen RID30.  C1 and every C2 component
# remain explicitly OFF so the resulting PERF_ACCEPT windows have one delta:
# Player::DrawBullets frontend stamps plus the already-existing M counter.
psp3000-player-shot-perf-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 PSP_PERF_PLAYER_SHOT=1 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_ITEM_MOTION_UPDATE=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0 PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_ADAPTIVE_AUX_RENDER=1 PSP_ME_ITEM_PREFIX_SPLIT=1 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=0x26090131u \
		PSP_EBOOT_TITLE='TH07 PSP PL M PERF' PSP_EASY_MIST_AUDIO=0 all

# C1 is deliberately research-only.  These three targets differ from the
# frozen RID30 comparison profile only in the packed stream component(s) and
# an explicit GE experiment gate.  Nothing here deploys an EBOOT.  On-device
# promotion requires the framebuffer+depth readback matrix in
# docs/C1_16BIT_VERTEX_VALIDATION.md.
PSP_C1_BUILD_UV16 ?= 1
PSP_C1_BUILD_XYZ16 ?= 1
PSP_C1_BUILD_ID ?= 0x260831c3u
PSP_C1_BUILD_TITLE ?= TH07 PSP C1 UVXYZ16 M0

psp3000-c1-uv16-m0-build: PSP_C1_BUILD_UV16=1
psp3000-c1-uv16-m0-build: PSP_C1_BUILD_XYZ16=0
psp3000-c1-uv16-m0-build: PSP_C1_BUILD_ID=0x260831c1u
psp3000-c1-uv16-m0-build: PSP_C1_BUILD_TITLE=TH07 PSP C1 UV16 M0
psp3000-c1-uv16-m0-build: override PSP_PERF_PLAYER_SHOT=0
psp3000-c1-uv16-m0-build: psp3000-c1-vertex16-m0-build

psp3000-c1-xyz16-m0-build: PSP_C1_BUILD_UV16=0
psp3000-c1-xyz16-m0-build: PSP_C1_BUILD_XYZ16=1
psp3000-c1-xyz16-m0-build: PSP_C1_BUILD_ID=0x260831c2u
psp3000-c1-xyz16-m0-build: PSP_C1_BUILD_TITLE=TH07 PSP C1 XYZ16 M0
psp3000-c1-xyz16-m0-build: override PSP_PERF_PLAYER_SHOT=0
psp3000-c1-xyz16-m0-build: psp3000-c1-vertex16-m0-build

psp3000-c1-uvxyz16-m0-build: PSP_C1_BUILD_UV16=1
psp3000-c1-uvxyz16-m0-build: PSP_C1_BUILD_XYZ16=1
psp3000-c1-uvxyz16-m0-build: PSP_C1_BUILD_ID=0x260831c3u
psp3000-c1-uvxyz16-m0-build: PSP_C1_BUILD_TITLE=TH07 PSP C1 UVXYZ16 M0
psp3000-c1-uvxyz16-m0-build: override PSP_PERF_PLAYER_SHOT=0
psp3000-c1-uvxyz16-m0-build: psp3000-c1-vertex16-m0-build

psp3000-c1-vertex16-m0-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 PSP_PERF_PLAYER_SHOT=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_ITEM_MOTION_UPDATE=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_UV16=$(PSP_C1_BUILD_UV16) PSP_ME_RENDER_XYZ16=$(PSP_C1_BUILD_XYZ16) PSP_ME_RENDER_16BIT_GE_EXPERIMENT=1 PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0 PSP_ME_ADAPTIVE_AUX_RENDER=1 PSP_ME_ITEM_PREFIX_SPLIT=1 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=$(PSP_C1_BUILD_ID) \
		PSP_EBOOT_TITLE='$(PSP_C1_BUILD_TITLE)' PSP_EASY_MIST_AUDIO=0 all

# C2 PC-only arena compaction matrix.  The first three targets isolate one
# arena at a time; c2abc_all_slim is the cumulative candidate.  C1 stays OFF,
# and no target below performs deployment.
PSP_C2_OUTPUT_SLIM ?= 0
PSP_C2_BULLET_SEED_SLIM ?= 0
PSP_C2_ITEM_SEED_SLIM ?= 0
PSP_C2_BUILD_ID ?= 0x260831dfu
PSP_C2_BUILD_TITLE ?= TH07 PSP C2 ABC SLIM

c2a_output_slim:
	@# Contract: PSP_ME_BULLET_OUTPUT_SLIM=1 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=0
	$(MAKE) PSP_1000=0 PSP_PERF_PLAYER_SHOT=0 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 \
		PSP_C2_OUTPUT_SLIM=1 PSP_C2_BULLET_SEED_SLIM=0 PSP_C2_ITEM_SEED_SLIM=0 \
		PSP_C2_BUILD_ID=0x260831d1u PSP_C2_BUILD_TITLE='TH07 PSP C2A OUTPUT SLIM' \
		psp3000-c2-slim-build

c2b_bullet_seed_slim:
	@# Contract: PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=1 PSP_ME_ITEM_SEED_SLIM=0
	$(MAKE) PSP_1000=0 PSP_PERF_PLAYER_SHOT=0 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 \
		PSP_C2_OUTPUT_SLIM=0 PSP_C2_BULLET_SEED_SLIM=1 PSP_C2_ITEM_SEED_SLIM=0 \
		PSP_C2_BUILD_ID=0x260831d2u PSP_C2_BUILD_TITLE='TH07 PSP C2B BULLET SEED SLIM' \
		psp3000-c2-slim-build

c2c_item_seed_slim:
	@# Contract: PSP_ME_BULLET_OUTPUT_SLIM=0 PSP_ME_BULLET_SEED_SLIM=0 PSP_ME_ITEM_SEED_SLIM=1
	$(MAKE) PSP_1000=0 PSP_PERF_PLAYER_SHOT=0 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 \
		PSP_C2_OUTPUT_SLIM=0 PSP_C2_BULLET_SEED_SLIM=0 PSP_C2_ITEM_SEED_SLIM=1 \
		PSP_C2_BUILD_ID=0x260831d3u PSP_C2_BUILD_TITLE='TH07 PSP C2C ITEM SEED SLIM' \
		psp3000-c2-slim-build

c2abc_all_slim:
	@# Contract: PSP_ME_BULLET_OUTPUT_SLIM=1 PSP_ME_BULLET_SEED_SLIM=1 PSP_ME_ITEM_SEED_SLIM=1
	$(MAKE) PSP_1000=0 PSP_PERF_PLAYER_SHOT=0 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 \
		PSP_C2_OUTPUT_SLIM=1 PSP_C2_BULLET_SEED_SLIM=1 PSP_C2_ITEM_SEED_SLIM=1 \
		PSP_C2_BUILD_ID=0x260831dfu PSP_C2_BUILD_TITLE='TH07 PSP C2 ABC SLIM' \
		psp3000-c2-slim-build

psp3000-c2-slim-build:
	$(MAKE) clean
	rm -f .build-profile-*
	$(MAKE) PSP_1000=0 PSP_DIRECT_GAME=0 PSP_DIRECT_MUSIC=0 PSP_PERF_DIAG=1 \
		PSP_PERF_PROFILE=PERF_ACCEPT PSP_PERF_ATTRIB_TARGET=M2 PSP_PERF_GPU_ATTRIB=0 PSP_PERF_PLAYER_SHOT=0 \
		PSP_PERF_EMPTY_TIMERS=0 PSP_PERF_DENSE_SLICE=1 PSP_ME_RENDER_WORKER=1 PSP_ME_RENDER_CORRECTNESS=1 PSP_ME_RENDER_RETIRE_DIAG=0 PSP_ME_RENDER_GE_CONSUME=1 PSP_ME_RENDER_PERFORMANCE=1 PSP_ME_RENDER_RAW_LIVE=1 PSP_ME_RENDER_DIRECT_LIST=1 PSP_ME_BULLET_FAST_UPDATE=0 PSP_ME_BULLET_COMPACT_UPDATE=1 PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY=0 PSP_ME_ITEM_RENDER_STREAM=1 PSP_ME_ITEM_MOTION_UPDATE=1 PSP_ME_EFFECT_RENDER_STREAM=0 PSP_ME_RENDER_UV16=0 PSP_ME_RENDER_XYZ16=0 PSP_ME_RENDER_16BIT_GE_EXPERIMENT=0 PSP_ME_BULLET_OUTPUT_SLIM=$(PSP_C2_OUTPUT_SLIM) PSP_ME_BULLET_SEED_SLIM=$(PSP_C2_BULLET_SEED_SLIM) PSP_ME_ITEM_SEED_SLIM=$(PSP_C2_ITEM_SEED_SLIM) PSP_ME_ADAPTIVE_AUX_RENDER=1 PSP_ME_ITEM_PREFIX_SPLIT=1 PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=0 PSP_ME_STARTUP_BREADCRUMBS=1 PSP_BULLET_COLLISION_BROADPHASE=1 \
		PSP_DIRECT_TRANSITION_TEST=0 PSP_SHIKIGAMI=1 PSP_MECC_BGM_384K=0 \
		PSP_MECC_AUDIO_4M=1 PSP_BULLET_AXIS_FAST=0 PSP_BULLET_SNAPSHOT_EMITTER=0 \
		PSP_BULLET_ROTATED_DIRECT=1 PSP_BULLET_UNIFIED_QUADS=1 PSP_BULLET_ONEPASS_ROTATED=1 \
		PSP_BULLET_HOT_PREFETCH=0 PSP_BULLET_WARM_QUEUE=0 PSP_BULLET_STATIC_PROXY=0 \
		PSP_ENEMY_P5_WARM_QUEUE=0 PSP_BULLET_QUIESCENT_ANM=0 PSP_ASCII_POPUP_BATCH=1 PSP_GUI_TILE_BATCH=0 \
		PSP_FONT_MAIN_RAM=1 PSP_TEXT_BLIT_FAST=1 PSP_TEXT_PREWARM_PROFILE=1 \
		PSP_USAGE_METER=1 PSP_USAGE_METER_TOGGLE=0 PSP_AUDIO4M_BUILD_ID=$(PSP_C2_BUILD_ID) \
		PSP_EBOOT_TITLE='$(PSP_C2_BUILD_TITLE)' PSP_EASY_MIST_AUDIO=0 all

# v0.2.1-beta 配布用: A6v4.1(CP932 wave)+A7 stage6 font-tail fix構成。
# SHIKIGAMIはホスト未設定で完全休眠
psp3000-dist-v021-build:
	$(MAKE) PSP_1000=0 PSP_RID30_AB_ME_UV16=0 PSP_RID30_AB_ME_XYZ16=0 \
		PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0 \
		PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0 \
		PSP_RID30_AB_ME_SEED_SOA=0 \
		PSP_RID30_AB_ME_POSITION_SOA_SHADOW=0 \
		PSP_RID30_AB_ME_TITLE_WORKSPACE=1 \
		PSP_RID30_AB_ME_TITLE_TRANSIENT=0 \
		PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1 \
		PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1 \
		PSP_RID30_AB_ME_FONT_TAIL_ARCHIVE=1 \
		PSP_SHIKIGAMI_HOST_IPV4= PSP_RID30_AB_ME_BUILD_ID=0x26090141u \
		PSP_RID30_AB_ME_TITLE='TH07 PSP v0.2.1-beta' \
		psp3000-rid30-ab-me-build
