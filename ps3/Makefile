#----------------------------------------------------------------------------
# NBlood on the PS3 (NBlood + PSL1GHT)
#
# From the root of the tree (~/NBlood-PS3):
#
#   make -C ps3            -> ps3/nblood-ps3-<version>.pkg (Blood)
#   make -C ps3 GAME=rr    -> ps3/redneck-ps3-<version>.pkg (Redneck Rampage)
#   make -C ps3 GAME=rrra  -> ps3/redneckra-ps3-<version>.pkg (Rides Again)
#   make -C ps3 clean
#   make -C ps3 verify     -> patch level of the tree and of the PKG
#   make -C ps3 check-toolchain
#
# The engine and the game are NBlood's own sources (EDuke32's engine),
# untouched but for a few #ifdef __PPU__ / RENDERTYPEPS3 lines. The PS3
# layer is EDuke32-PS3's. The PS3 side is ps3/source:
#  - ps3layer.cpp is the Build "render layer" (sdlayer.cpp's job): boot,
#    sysutil, video mode and frames on the RSX, the DualShock 3, message
#    boxes, the clock, the ways out;
#  - driver_ps3.cpp is the audiolib's PCM driver on libaudio;
#  - ps3_gpu.c, ps3_video.c, ps3_pad.c, ps3_log.c: shared with the 8 bit
#    ports (CrispyCell, ECellWolf, ROTT-PS3).
#
# What the engine calls from the C library that must behave differently
# on the PS3 (exit, printf and friends, functions taking paths) is renamed
# in its objects to the PS3_* versions (ps3/source/ps3_stdio.c), after
# they are linked into one object (the ROTT-PS3 way).
#
# Packaging (.elf -> .self -> .pkg) is done by ppu_rules' own pattern rules
# (strip + sprxlinker + sign + package_finalize). Never by hand.
#----------------------------------------------------------------------------

ifeq ($(strip $(PS3DEV)),)
$(error PS3DEV is not set. Do: export PS3DEV=/usr/local/ps3dev)
endif

ifeq ($(strip $(PSL1GHT)),)
export PSL1GHT := $(PS3DEV)
endif

include $(PS3DEV)/ppu_rules

# (ppu_rules and the per-file rules below come before 'all')
.DEFAULT_GOAL := all

#----------------------------------------------------------------------------
# Package identity
#
# APPID: EXACTLY 9 characters. ':=' and not '?=': ppu_rules sets
# TITLE/APPID with '?=' BEFORE these lines (handoff notes 3.15, 3.16).
#----------------------------------------------------------------------------

VERSION    := 1.0
PATCHLEVEL := $(shell cat $(CURDIR)/PATCHLEVEL 2>/dev/null || echo 0)
TARGET     := nblood-ps3
TITLE      := NBlood
APPID      := NBLOODPS3
CONTENTID  := UP0001-$(APPID)_00-0000000000000000
PKGFILES   := $(CURDIR)/pkgfiles
ICON0      := $(CURDIR)/ICON0.PNG
OUT_PKG    := nblood-ps3-$(VERSION).pkg

SRC        := $(abspath $(CURDIR)/..)
ESRC       := $(SRC)/source
OBJDIR     := $(CURDIR)/obj
BUILDDIR   := $(CURDIR)/build

# The game: NBlood (Blood, the default), or Rednukem with GAME=rr
# (Redneck Rampage) and GAME=rrra (Redneck Rampage Rides Again): one
# program for both, two packages (both games' data is a REDNECK.GRP, so
# each has its own USRDIR). PCExhumed (PowerSlave) lives in the same tree
# for later. Each game builds in its own folders.
GAME       ?= blood

ifeq ($(GAME),blood)
GAME_SRC   := blood
else ifeq ($(GAME),rr)
TARGET     := redneck-ps3
TITLE      := Redneck Rampage
APPID      := REDNECKRM
GAME_SRC   := rr
else ifeq ($(GAME),rrra)
TARGET     := redneckra-ps3
TITLE      := Redneck Rampage Rides Again
APPID      := REDNECKRA
GAME_SRC   := rr
else
$(error GAME is blood, rr or rrra)
endif

ifneq ($(GAME),blood)
CONTENTID  := UP0001-$(APPID)_00-0000000000000000
PKGFILES   := $(CURDIR)/pkgfiles-$(GAME)
ICON0      := $(CURDIR)/ICON0-$(GAME).PNG
OUT_PKG    := $(TARGET)-$(VERSION).pkg
OBJDIR     := $(CURDIR)/obj-$(GAME)
BUILDDIR   := $(CURDIR)/build-$(GAME)
endif

# DEBUG=1: the testing build (PS3_DEBUG_CHEATS): always immortal, every
# weapon with ammo that doesn't run out, all the keys, L3 + R3 together
# ends the level. Its own PKG (<target>-debug-<version>.pkg, "<title>
# (debug)") and folders; the same APPID, so it installs over the game and
# uses its data (the normal PKG installed again goes back).
DEBUG ?= 0
ifeq ($(DEBUG),1)
TARGET     := $(TARGET)-debug
TITLE      := $(TITLE) (debug)
OUT_PKG    := $(TARGET)-$(VERSION).pkg
PKGFILES   := $(PKGFILES)-debug
OBJDIR     := $(OBJDIR)-debug
BUILDDIR   := $(BUILDDIR)-debug
endif

CC         := ppu-gcc
CXX        := ppu-g++
LD         := ppu-g++
PARTIALLD  := ppu-ld
OBJCOPY    := ppu-objcopy

BUILD_DATE := $(shell date +%Y-%m-%d)

#----------------------------------------------------------------------------
# Flags
#----------------------------------------------------------------------------

# EDuke32's own (GNUmakefile, release, no Polymer / network / startup
# window / FLAC), plus the PS3: big-endian, no SDL (RENDERTYPEPS3).
# OpenGL (Polymost) goes to the RSX through ps3gl.
COMMON := -O2 -funswitch-loops -fomit-frame-pointer -fno-strict-aliasing -fno-stack-protector
COMMON += -funsigned-char -mcpu=cell $(MACHDEP)
COMMON += -Wall -Wno-char-subscripts -Wno-missing-braces -Wno-attributes -Wno-strict-overflow
COMMON += -Wno-unused-result -Wno-unknown-pragmas -Wno-format-truncation -Wno-stringop-overflow
COMMON += -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
# PPU is LP64: a missing prototype truncates pointers to int. Hard errors
# (handoff notes 3.26).
COMMON += -Werror=implicit-function-declaration -Werror=int-conversion
COMMON += -MMD -MP

COMMON += -D_FORTIFY_SOURCE=0 -DNDEBUG -DNOASM -DNETCODE_DISABLE -DRENDERTYPEPS3=1
COMMON += -DHAVE_VORBIS -DHAVE_XMP -D_GNU_SOURCE=1 -DUSE_OPENGL
COMMON += -DB_LITTLE_ENDIAN=0 -DB_BIG_ENDIAN=1
COMMON += -D__PS3__ -DPS3_APPID=\"$(APPID)\" -DPS3_BUILD_DATE=\"$(BUILD_DATE)\"
COMMON += -DPS3_VERSION=\"$(VERSION)\" -DPS3_PATCHLEVEL=$(PATCHLEVEL)
ifeq ($(DEBUG),1)
COMMON += -DPS3_DEBUG_CHEATS
endif
ifeq ($(GAME),blood)
COMMON += -DPS3_GAME_BLOOD -DNOONE_EXTENSIONS
else
COMMON += -DPS3_GAME_RR
ifeq ($(GAME),rrra)
COMMON += -DPS3_GAME_RRRA
endif
endif

INCLUDES := -I$(ESRC)/build/include -I$(ESRC)/mact/include -I$(ESRC)/audiolib/include
INCLUDES += -I$(ESRC)/libxmp-lite/include -I$(ESRC)/glad/include -I$(CURDIR)/source -I$(CURDIR)/compat
INCLUDES += -I$(ESRC)/libsmackerdec/include
INCLUDES += $(LIBPSL1GHT_INC)

CFORCE   := -include $(CURDIR)/compat/ps3_compat.h

CFLAGS   := -std=gnu11 $(COMMON) $(INCLUDES) $(CFORCE)
CXXFLAGS := -std=gnu++14 -fno-exceptions -fno-rtti -fno-threadsafe-statics $(CXXFLAGS_INIT)
CXXFLAGS += $(COMMON) $(INCLUDES) $(CFORCE)
# the start log: each C++ file says when its static initializers begin
CXXFLAGS += -include $(CURDIR)/compat/ps3_initmark.h

XMPFLAGS := -DHAVE_ROUND -DLIBXMP_CORE_PLAYER -DLIBXMP_NO_PROWIZARD -DLIBXMP_NO_DEPACKERS
XMPFLAGS += -DBUILDING_STATIC -I$(ESRC)/libxmp-lite/include/libxmp-lite
XMPFLAGS += -Wno-unused-parameter -Wno-sign-compare

# The PS3 layer itself: plain C, warnings as in the other ports.
PCFLAGS  := -O2 -std=gnu99 -Wall -fno-strict-aliasing -fno-common -mcpu=cell $(MACHDEP)
PCFLAGS  += -Werror=implicit-function-declaration -Werror=int-conversion -MMD -MP
PCFLAGS  += -DPS3_APPID=\"$(APPID)\" -DPS3_BUILD_DATE=\"$(BUILD_DATE)\"
PCFLAGS  += -I$(CURDIR)/source -I$(CURDIR)/compat $(LIBPSL1GHT_INC)
ifeq ($(GAME),blood)
PCFLAGS  += -DPS3_GAME_BLOOD
else
PCFLAGS  += -DPS3_GAME_RR
ifeq ($(GAME),rrra)
PCFLAGS  += -DPS3_GAME_RRRA
endif
endif

# -lrt and -llv2 are MANDATORY: without them the binary links cleanly, the
# PKG installs, and it never reaches main(). Black screen, back to the XMB,
# no log at all (handoff notes 3.1).
# (net: only to resolve the old networking code in mmulti.cpp, which the
# port never calls -- no network play)
LIBS := gcm_sys rsx sysutil io audio net sysmodule rt lv2 m

#----------------------------------------------------------------------------
# Sources (as GNUmakefile builds eduke32 with RENDERTYPE=SDL, minus the
# SDL layer and driver, ALSA and FLAC)
#----------------------------------------------------------------------------

# (no enet.cpp: no network play)
BUILD_CPP := 2d a-c baselayer cache1d clip colmatch common communityapi compat cpuid \
	crc32 defs dxtfilter engine fix16 hash hightile klzw kplib loguru md4 mhk \
	mmulti mutex osd palette pngwrite pragmas rev screenshot screentext scriptfile \
	sjson smalltextfont smmalloc smmalloc_generic smmalloc_tls softsurface texcache \
	textfont tiles timer vfs \
	glbuild glsurface mdsprite polymost tilepacker voxmodel
BUILD_C   := lz4 miniz miniz_tdef miniz_tinfl xxhash

# Polymost's GLSL, as text in the program (GNUmakefile's rule): ps3gl
# recognizes it and runs its Cg version (source/gl/shaders/polymost_*).
GLSL      := polymost1Frag polymost1Vert

# NBlood's (GNUmakefile's blood_game_objs, NOONE_EXTENSIONS on, without
# the startup windows)
BLOOD_CPP := blood actor ai aibat aibeast aiboneel aiburn aicaleb aicerber aicult \
	aigarg aighost aigilbst aihand aihound aiinnoc aipod airat aispid aitchern \
	aizomba aizombf asound barf callback choke common config controls credits db \
	demo dude endgame eventq fire fx gamemenu gameutil getopt gfx gib globals gui \
	inifile iob levels loadsave map2d menu messages mirrors misc network osdcmd \
	player qav qheap replace resource screen sectorfx seq sfx sound tile trig \
	triggers view warp weapon ps3_blood \
	nnextsif nnexts nnextstr nnextcitem nnextslaser nnextcdud aicdud aipatrol

# The Smacker videos (the intro, Blood's cutscenes)
SMK_CPP   := BitReader FileStream HuffmanVLC LogError SmackerDecoder

AUDIO_CPP := driver_adlib driver_sf2 drivers flac formats fx_man gmtimbre midi mix \
	mixst multivoc music opl3 pitch vorbis xa xmp

MACT_CPP  := animlib control joystick keyboard scriplib

XMP_C     := common control dataio effects filter format hio it_load itsex lfo load \
	load_helpers memio mix_all mixer mod_load mtm_load period player read_event \
	s3m_load sample scan smix virtual win32 xm_load

# Rednukem's (GNUmakefile's rr_game_objs and its n64 folder, Duke 64's
# code: game.cpp calls it)
RR_CPP    := game global actors gamedef gameexec gamevars player premap sector anim \
	common config demo input menus namesdyn net savegame rts osdfuncs osdcmds \
	grpscan sounds soundsdyn cheats sbar screentext screens cmdline rrdh \
	filestream playmve \
	$(addprefix n64/,reality reality_music reality_player reality_render \
		reality_sbar reality_screens reality_sound reality_util)

# The PS3 pieces that are part of the game (renamed with the engine):
# what the layer asks of the game.
ifeq ($(GAME),blood)
GAME_CPP    := $(BLOOD_CPP)
GAMEPS3_CPP := ps3_game_blood
else
GAME_CPP    := $(RR_CPP)
GAMEPS3_CPP := ps3_game_rr
endif

ENGINE_OBJS := \
	$(patsubst %,$(OBJDIR)/build/%.o,$(BUILD_CPP) $(BUILD_C)) \
	$(patsubst %,$(OBJDIR)/glsl/%.o,$(GLSL)) \
	$(OBJDIR)/glad/glad.o \
	$(patsubst %,$(OBJDIR)/$(GAME_SRC)/%.o,$(GAME_CPP)) \
	$(patsubst %,$(OBJDIR)/audiolib/%.o,$(AUDIO_CPP)) \
	$(patsubst %,$(OBJDIR)/mact/%.o,$(MACT_CPP)) \
	$(patsubst %,$(OBJDIR)/libxmp/%.o,$(XMP_C)) \
	$(patsubst %,$(OBJDIR)/smk/%.o,$(SMK_CPP)) \
	$(patsubst %,$(OBJDIR)/gameps3/%.o,$(GAMEPS3_CPP))

# The PS3 layer: talks to the engine (its headers) but calls the real C
# library (not renamed).
LAYER_CPP := ps3layer driver_ps3 ps3_gladfill
LAYER_C   := ps3_log ps3_stdio ps3_pad ps3_video ps3_debug ps3_prof ps3_sf2spu ps3_launcher

# ps3gl: OpenGL on the RSX, TyrQuakeCell's (ps3/source/gl), plus what
# Polymost needs (ps3gl_prog.c: its GLSL program as Cg, buffer objects;
# ps3gl_procs.c: the entry points for glad). The display layer
# (ps3_video.c) is TyrQuakeCell's too: one RSX owner for both the software
# image and the OpenGL render target.
PS3GL_C   := ps3gl_main ps3gl_state ps3gl_texture ps3gl_draw ps3gl_prog ps3gl_procs

LAYER_OBJS := $(patsubst %,$(OBJDIR)/ps3/%.o,$(LAYER_CPP) $(LAYER_C)) \
	$(patsubst %,$(OBJDIR)/ps3/gl/%.o,$(PS3GL_C))

# The SoundFont synth's voices on an SPU (ps3/spu/sf2spu.c), when the
# toolchain has spu-gcc (ps3toolchain builds it); without it, or with
# "make SPU=0", they render on the PPU as before.
SPU_GCC := $(PS3DEV)/spu/bin/spu-gcc
ifeq ($(SPU),)
SPU := $(shell test -x $(SPU_GCC) && echo 1 || echo 0)
endif

ifeq ($(SPU),1)
LAYER_OBJS += $(OBJDIR)/spu/sf2spu_bin.o
$(OBJDIR)/audiolib/driver_sf2.o: CXXFLAGS += -DPS3_SF2_SPU
$(OBJDIR)/ps3/ps3_sf2spu.o: PCFLAGS += -DPS3_SF2_SPU
endif

SPUFLAGS := -O3 -ffast-math -funroll-loops -fschedule-insns -mdual-nops -Wall -I$(CURDIR)/source

# The audio mixer and the SoundFont synth run all the time, in the audio
# thread: the fastest code they can get.
AUDIO_FAST := $(patsubst %,$(OBJDIR)/audiolib/%.o,driver_sf2 mix mixst multivoc)
$(AUDIO_FAST): CXXFLAGS += -O3 -ffast-math

# The C library calls the engine makes that go to the PS3 versions.
REDEFINE := exit abort printf fprintf vfprintf vprintf puts putchar fputs fputc \
	putc fwrite fflush perror open fopen stat access mkdir opendir remove \
	unlink rename getcwd chdir
REDEFINE_FLAGS := $(foreach s,$(REDEFINE),--redefine-sym $(s)=PS3_$(s))

# The file that talks to GCM the most goes at -O1 -fno-inline: PSL1GHT's
# syscall stubs don't survive aggressive inlining (handoff notes 4.2).
$(OBJDIR)/ps3/ps3_video.o: PCFLAGS := $(filter-out -O2,$(PCFLAGS)) -O1 -fno-inline -DUSE_OPENGL

# The TOC (handoff notes 3.100): ppu-gcc 7.2 has no -mcmodel, the TOC
# holds 64 KB and the link must not split it (--no-multi-toc: a split TOC
# means r2off stubs, and a crash at boot). Everything that runs at load
# time, in the menus or in the console goes through -mminimal-toc (one
# TOC entry per file); the renderer, the game's VM and the per-tic game
# logic keep direct TOC access. 'make check' (after every link) fails the
# build if .got reaches 0x10000 or any r2off stub shows up.
ifeq ($(GAME_SRC),rr)
MINTOC_GAME := $(patsubst %,$(OBJDIR)/rr/%.o,menus osdcmds osdfuncs config cmdline \
	grpscan savegame demo anim premap rts gamedef global cheats net common screens \
	screentext sbar rrdh filestream playmve namesdyn soundsdyn \
	$(addprefix n64/,reality reality_music reality_player reality_render \
		reality_sbar reality_screens reality_sound reality_util))
else
MINTOC_GAME := $(patsubst %,$(OBJDIR)/blood/%.o,menu gamemenu config osdcmd levels \
	loadsave credits demo network inifile getopt common resource iob endgame \
	messages replace barf controls blood qheap map2d tile nnextsif nnextstr \
	nnextcitem aibat aibeast aiboneel aiburn aicaleb aicerber aicult aigarg \
	aighost aigilbst aihand aihound aiinnoc aipod airat aispid aitchern aizomba \
	aizombf aicdud aipatrol nnextslaser nnextcdud)
endif
MINTOC_GAME += $(patsubst %,$(OBJDIR)/smk/%.o,$(SMK_CPP))

MINTOC_OBJS := \
	$(MINTOC_GAME) \
	$(patsubst %,$(OBJDIR)/build/%.o,defs osd loguru kplib compat scriptfile common \
		baselayer mhk md4 hightile sjson cache1d colmatch pngwrite screenshot crc32 \
		communityapi cpuid rev smmalloc smmalloc_generic smmalloc_tls texcache \
		dxtfilter klzw lz4 miniz miniz_tdef miniz_tinfl xxhash \
		glbuild glsurface mdsprite voxmodel tilepacker) \
	$(OBJDIR)/glad/glad.o \
	$(patsubst %,$(OBJDIR)/audiolib/%.o,vorbis xmp formats xa flac midi music \
		driver_sf2 gmtimbre drivers fx_man) \
	$(patsubst %,$(OBJDIR)/mact/%.o,scriplib animlib) \
	$(patsubst %,$(OBJDIR)/libxmp/%.o,$(XMP_C))

$(MINTOC_OBJS): CFLAGS += -mminimal-toc
$(MINTOC_OBJS): CXXFLAGS += -mminimal-toc

# Function trace (ps3/source/ps3_debug.c): what's in FTRACE_OBJS logs its
# calls into a ring in memory, written to the log if the game thread dies
# (no crash handler on a retail console). The engine's headers (small
# inline helpers) stay out, or they'd fill the ring.
FTRACE_FLAGS := -finstrument-functions \
	-finstrument-functions-exclude-file-list=/build/include/,/include/c++/,/compat/
FTRACE_OBJS := \
	$(patsubst %,$(OBJDIR)/build/%.o,engine polymost glbuild glsurface \
		texcache hightile mdsprite voxmodel palette baselayer 2d) \
	$(patsubst %,$(OBJDIR)/$(GAME_SRC)/%.o,$(GAME_CPP)) \
	$(OBJDIR)/ps3/ps3layer.o
FTRACE_C_OBJS := $(patsubst %,$(OBJDIR)/ps3/gl/%.o,$(PS3GL_C))

# Off by default since patch 17: every call of the renderer went through
# the ring, a real cost per frame. "make -C ps3 FTRACE=1" (after a clean)
# for a build that leaves the trail. The game's own code is in it too
# (NBlood-PS3 patch 9: Blood died starting a level, in its code or the
# renderer's).
ifeq ($(FTRACE),1)
$(FTRACE_OBJS): CXXFLAGS += $(FTRACE_FLAGS)
$(FTRACE_C_OBJS): PCFLAGS += $(FTRACE_FLAGS)
$(OBJDIR)/ps3/ps3_debug.o: PCFLAGS += -DPS3_FTRACE
endif

# ps3gl and the display write RSX memory, which is cache inhibited: a
# misaligned access there is an alignment interrupt lv2 doesn't handle
# (the thread dies). GCC 7's store merging joins neighbouring stores into
# wider ones (two floats -> one 8 byte std) without caring about that.
$(FTRACE_C_OBJS) $(OBJDIR)/ps3/ps3_video.o: PCFLAGS += -fno-store-merging

# The logger: std::recursive_mutex and friends on lv2 (no std threads on
# the PPU), no pthreads, no stack traces.
$(OBJDIR)/build/loguru.o: CXXFLAGS += -include $(CURDIR)/compat/ps3_threads.h \
	-DLOGURU_PTHREADS=0 -DLOGURU_WINTHREADS=0 -DLOGURU_STACKTRACES=0

# The build's name, patch level and date in the engine's log and console:
# rebuilt every time (a stale rev.o kept saying the previous patch level)
$(OBJDIR)/build/rev.o: CXXFLAGS += -DREV=NBlood-PS3-p$(PATCHLEVEL)
$(OBJDIR)/build/rev.o: FORCE

#----------------------------------------------------------------------------
# Rules
#----------------------------------------------------------------------------

.PHONY: all clean check verify check-toolchain pkgdirs FORCE

all: $(TARGET).pkg
	@cp $(TARGET).gnpdrm.pkg $(OUT_PKG)
	@echo ""
	@echo "PKG to install: $(CURDIR)/$(OUT_PKG)  (patch level $(PATCHLEVEL))"
	@ls -la $(OUT_PKG)

$(OBJDIR)/build/%.o: $(ESRC)/build/src/%.cpp
	@mkdir -p $(dir $@)
	@echo "CXX build/$*.cpp"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/build/%.o: $(ESRC)/build/src/%.c
	@mkdir -p $(dir $@)
	@echo "CC  build/$*.c"
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/glsl/%.cpp: $(ESRC)/build/src/%.glsl
	@mkdir -p $(dir $@)
	@echo "GLSL build/$*.glsl"
	@printf 'extern char const *%s;\nchar const *%s = R"shader(' $* $* > $@
	@cat $< >> $@
	@printf ')shader";\n' >> $@

$(OBJDIR)/glsl/%.o: $(OBJDIR)/glsl/%.cpp
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/glad/glad.o: $(ESRC)/glad/src/glad.c
	@mkdir -p $(dir $@)
	@echo "CC  glad/glad.c"
	@$(CC) $(CFLAGS) -c $< -o $@

# NBlood's own warnings, as upstream builds it (not this port's to fix)
BLOODWARN := -Wno-sign-compare -Wno-maybe-uninitialized -Wno-format-overflow
# Blood is full of #pragma pack(1) structs: GCC reads and writes their
# misaligned 8 byte fields with lfd/stfd (a pointer copied through an FP
# register), and a misaligned FP access is an alignment interrupt lv2
# leaves unhandled: the thread dies (patch 5: nnextcdud.cpp static
# initializers, before main). Byte by byte where a field may be misaligned.
BLOODWARN += -mstrict-align

$(OBJDIR)/blood/%.o: $(ESRC)/blood/src/%.cpp
	@mkdir -p $(dir $@)
	@echo "CXX blood/$*.cpp"
	@$(CXX) $(CXXFLAGS) $(BLOODWARN) -I$(ESRC)/blood/src -c $< -o $@

# Rednukem's: its own warnings off as upstream builds it; packed structs
# too (actors.h, player.h...), so the same care as Blood's
RRWARN := -Wno-sign-compare -Wno-maybe-uninitialized -Wno-format-overflow -mstrict-align

$(OBJDIR)/rr/%.o: $(ESRC)/rr/src/%.cpp
	@mkdir -p $(dir $@)
	@echo "CXX rr/$*.cpp"
	@$(CXX) $(CXXFLAGS) $(RRWARN) -I$(ESRC)/rr/src -c $< -o $@

$(OBJDIR)/smk/%.o: $(ESRC)/libsmackerdec/src/%.cpp
	@mkdir -p $(dir $@)
	@echo "CXX libsmackerdec/$*.cpp"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/audiolib/%.o: $(ESRC)/audiolib/src/%.cpp
	@mkdir -p $(dir $@)
	@echo "CXX audiolib/$*.cpp"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/spu/sf2spu.elf: $(CURDIR)/spu/sf2spu.c $(CURDIR)/source/sf2spu_wire.h
	@mkdir -p $(dir $@)
	@echo "SPU spu/sf2spu.c"
	@$(SPU_GCC) $(SPUFLAGS) $< -o $@ -lm
	@$(PS3DEV)/spu/bin/spu-strip $@

$(OBJDIR)/spu/sf2spu_bin.o: $(OBJDIR)/spu/sf2spu.elf
	@cp $< $(OBJDIR)/spu/sf2spu.bin
	@bin2s -a 128 $(OBJDIR)/spu/sf2spu.bin | ppu-as -o $@

$(OBJDIR)/ps3/ps3_sf2spu.o $(OBJDIR)/audiolib/driver_sf2.o: $(CURDIR)/source/sf2spu_wire.h

$(OBJDIR)/mact/%.o: $(ESRC)/mact/src/%.cpp
	@mkdir -p $(dir $@)
	@echo "CXX mact/$*.cpp"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/libxmp/%.o: $(ESRC)/libxmp-lite/src/%.c
	@mkdir -p $(dir $@)
	@echo "CC  libxmp-lite/$*.c"
	@$(CC) $(CFLAGS) $(XMPFLAGS) -c $< -o $@

$(OBJDIR)/gameps3/%.o: $(CURDIR)/source/%.cpp
	@mkdir -p $(dir $@)
	@echo "CXX ps3/$*.cpp (game)"
	@$(CXX) $(CXXFLAGS) -I$(ESRC)/$(GAME_SRC)/src -c $< -o $@

$(OBJDIR)/ps3/%.o: $(CURDIR)/source/%.cpp
	@mkdir -p $(dir $@)
	@echo "CXX ps3/$*.cpp"
	@$(CXX) $(CXXFLAGS) -I$(ESRC)/build/src -c $< -o $@

$(OBJDIR)/ps3/%.o: $(CURDIR)/source/%.c
	@mkdir -p $(dir $@)
	@echo "CC  ps3/$*.c"
	@$(CC) $(PCFLAGS) -c $< -o $@

$(OBJDIR)/ps3/gl/%.o: $(CURDIR)/source/gl/%.c
	@mkdir -p $(dir $@)
	@echo "CC  ps3/gl/$*.c"
	@$(CC) $(PCFLAGS) -c $< -o $@

# The engine and the game in one object, the C library calls renamed.
$(OBJDIR)/engine.o: $(ENGINE_OBJS)
	@echo "LD  engine.o (C library calls -> PS3_*)"
	@$(PARTIALLD) -r -o $(OBJDIR)/engine_all.o $(ENGINE_OBJS)
	@$(OBJCOPY) $(REDEFINE_FLAGS) $(OBJDIR)/engine_all.o $@

$(TARGET).elf: $(OBJDIR)/engine.o $(LAYER_OBJS)
	@echo "LD  $@"
	@$(LD) $(MACHDEP) -Wl,--no-multi-toc $^ $(LIBPSL1GHT_LIB) -o $@.tmp $(foreach lib,$(LIBS),-l$(lib))
	@$(MAKE) --no-print-directory check ELF=$@.tmp
	@mv -f $@.tmp $@

-include $(ENGINE_OBJS:.o=.d) $(LAYER_OBJS:.o=.d)

#----------------------------------------------------------------------------
# PKG contents and icon
#
# pkgfiles/ mirrors the PKG, not its USRDIR. VERSION.TXT is always written
# (it tells which build is installed). Game data is NOT packaged: it goes
# by FTP into USRDIR.
#----------------------------------------------------------------------------

$(PKGFILES)/USRDIR/VERSION.TXT: FORCE
	@mkdir -p $(PKGFILES)/USRDIR
	@echo "$(TITLE) -- $(TARGET) $(VERSION) -- patch $(PATCHLEVEL) -- $(BUILD_DATE)" > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv -f $@.tmp $@; fi

$(ICON0):
	@echo "No ICON0.PNG, generating a 320x176 placeholder..."
	@python3 -c "import zlib,struct;w,h=320,176;\
rows=b''.join(b'\x00'+bytes([120,24,8,255])*w for _ in range(h));\
png=lambda t,d:struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d));\
open('$(ICON0)','wb').write(b'\x89PNG\r\n\x1a\n'+png(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+png(b'IDAT',zlib.compress(rows))+png(b'IEND',b''))"

# The folders the user fills. Created at build time: they are empty, and
# neither git nor a patch keeps empty folders. addons: the add-ons (and
# the game itself, if wanted) instead of USRDIR, each in a folder of its
# own if wanted (ps3_launcher.c).
PKG_DIRS := USRDIR/addons
ifeq ($(GAME_SRC),rr)
# Redneck Rampage's CD soundtrack: track02.ogg ... (ps3_launcher.c adds
# the folder to the search path)
PKG_DIRS += USRDIR/music
endif
ifeq ($(GAME),rr)
# Suckin' Grits on Route 66: its loose files over REDNECK.GRP
# (ps3_launcher.c), with its maps in addons/route66/ROUTE66
PKG_DIRS += USRDIR/addons/route66 USRDIR/addons/route66/ROUTE66
endif

pkgdirs:
	@for d in $(PKG_DIRS); do mkdir -p "$(PKGFILES)/$$d"; done

$(TARGET).pkg: $(PKGFILES)/USRDIR/VERSION.TXT $(ICON0) | pkgdirs

# NBlood's own data (its menu art, its blood.def): nblood.pk3, from the
# root of the tree, in USRDIR as on every other platform
ifeq ($(GAME),blood)
$(TARGET).pkg: $(PKGFILES)/USRDIR/nblood.pk3
endif

$(PKGFILES)/USRDIR/nblood.pk3: $(SRC)/nblood.pk3
	@mkdir -p $(dir $@)
	@cp $< $@

# RELOAD.SELF: this program as a plain SELF, straight into the folder the
# %.pkg rule packs. Quit and errors start it to come back to the launcher
# (sysProcessExitSpawn2 can't start the NPDRM EBOOT.BIN).
$(TARGET).pkg: reloadself

.PHONY: reloadself
reloadself: $(TARGET).self
	@mkdir -p $(BUILDDIR)/pkg/USRDIR
	@cp $(TARGET).self $(BUILDDIR)/pkg/USRDIR/RELOAD.SELF

#----------------------------------------------------------------------------
# Diagnostics
#----------------------------------------------------------------------------

# The TOC (see MINTOC_OBJS above).
ELF ?= $(TARGET).elf
check:
	@got=`ppu-readelf -SW $(ELF) | awk '$$2==".got"{print $$6}'`; \
	 stubs=`ppu-nm $(ELF) | grep -c r2off`; \
	 echo "TOC check: .got = 0x$$got bytes (limit 0x10000), r2off stubs = $$stubs"; \
	 if [ $$((0x$$got)) -ge 65536 ] || [ $$stubs -ne 0 ]; then \
	   echo "TOC check FAILED: move more objects to MINTOC_OBJS"; rm -f $(ELF); exit 1; fi

verify:
	@echo "tree patch level: $(PATCHLEVEL)"
	@if [ -f $(PKGFILES)/USRDIR/VERSION.TXT ]; then \
	   echo "PKG contents    : `cat $(PKGFILES)/USRDIR/VERSION.TXT`"; \
	 else echo "PKG contents    : (not built yet)"; fi
	@if [ -f $(OUT_PKG) ]; then ls -la $(OUT_PKG); fi

check-toolchain:
	@echo "PS3DEV   = $(PS3DEV)"
	@echo "ppu_rules: `test -f $(PS3DEV)/ppu_rules && echo OK || echo MISSING`"
	@$(CC) --version | head -1
	@echo "APPID    = $(APPID)  (length: `printf '%s' '$(APPID)' | wc -c`, MUST be 9)"
	@echo "TITLE    = $(TITLE)"
	@for l in $(LIBS); do \
	   if [ -f "$(PS3DEV)/ppu/lib/lib$$l.a" ] || [ -f "$(PSL1GHT)/ppu/lib/lib$$l.a" ]; then \
	     printf "  OK      lib%s.a\n" "$$l"; \
	   elif $(CC) -print-file-name=lib$$l.a | grep -q /; then \
	     printf "  OK      lib%s.a (gcc sysroot)\n" "$$l"; \
	   else \
	     printf "  MISSING lib%s.a\n" "$$l"; \
	   fi; \
	 done

clean:
	rm -rf $(OBJDIR) $(BUILDDIR)
	rm -f $(TARGET).elf $(TARGET).self $(TARGET).fake.self
	rm -f $(PKGFILES)/USRDIR/VERSION.TXT
	@echo "clean (the .pkg is left alone: ppu_rules rebuilds it)"
