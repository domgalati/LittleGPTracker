# pt_lab: PIGTAIL measurement harness. Not part of the default build.
#
# Needs the X64 objects first:  cd projects && make PLATFORM=X64
# Then, from tools/pt_lab:
#   make audit    96 kHz reference vs PIGTAIL at 44.1 kHz, 5 seeds (~10 s)
#   make tune     YIN pitch sweep, pitched models (~2-3 min)
#   make init     every model initialises and makes sound
#   make cpu      per model CPU cost (from the -g X64 objects)
#   make stress   shape switching under ASan + UBSan
#   make ident    bit-identity vs pristine upstream (f43d554) at 96 kHz
#   make all-tests
#
# BRAIDS_WRAPV: the X64 build compiles Braids with -fwrapv, so the sanitizer
# build does too. With -fwrapv UBSan doesn't check signed overflow; run
# `make stress BRAIDS_WRAPV=` to see upstream's overflow sites again.

ROOT     := $(abspath ../..)
OBJDIR   := $(ROOT)/projects/buildX64
BUILD    := build
PRISTINE := f43d554

CXX      ?= g++
DEFINES  := -DPLATFORM_X64 -D_64BIT -DCPP_MEMORY -D__LINUX_ALSA__ -D__LINUX_ALSASEQ__ \
            -DSDL2 -DSDLAUDIO -DRTMIDI -DFFMPEG_ENABLED -D_FEAT_MIDI_LOCK
INCLUDES := $(shell pkg-config sdl2 --cflags) -I$(ROOT)/sources -I$(ROOT)/sources/Externals/Braids
LIBS     := $(shell pkg-config alsa --libs) $(shell pkg-config jack --libs 2>/dev/null) \
            $(shell pkg-config sdl2 --libs) -lpthread
STD      := -std=gnu++03
BRAIDS_WRAPV ?= -fwrapv
SAN      := -fsanitize=address,undefined -fno-sanitize-recover=address

BRAIDS_SRC := $(wildcard $(ROOT)/sources/Externals/Braids/braids/*.cpp) \
              $(ROOT)/sources/Externals/Braids/stmlib/utils/random.cpp
PIGTAIL_SRC := $(ROOT)/sources/Application/Instruments/PigtailInstrument.cpp

# Objects from the X64 build: everything except main()
APP_OBJS  = $(filter-out $(OBJDIR)/LINUXMain.o,$(wildcard $(OBJDIR)/*.o))
# For the sanitizer build, also leave out what gets rebuilt instrumented
SAN_SKIP  = LINUXMain.o PigtailInstrument.o $(notdir $(BRAIDS_SRC:.cpp=.o))
SAN_OBJS  = $(filter-out $(addprefix $(OBJDIR)/,$(SAN_SKIP)),$(wildcard $(OBJDIR)/*.o))

.PHONY: lab asan ident audit tune init cpu stress all-tests check-objs clean

lab: $(BUILD)/pt_lab

check-objs:
	@test -f $(OBJDIR)/LINUXMain.o || { echo "pt_lab: no X64 objects in $(OBJDIR); run 'cd projects && make PLATFORM=X64' first"; exit 1; }

$(BUILD)/pt_lab: pt_lab.cpp check-objs
	@mkdir -p $(BUILD)
	$(CXX) -O2 -w $(STD) $(DEFINES) $(INCLUDES) -c pt_lab.cpp -o $(BUILD)/pt_lab.o
	$(CXX) -o $@ $(BUILD)/pt_lab.o $(APP_OBJS) $(LIBS)

asan: $(BUILD)/pt_lab_asan

$(BUILD)/pt_lab_asan: pt_lab.cpp check-objs
	@mkdir -p $(BUILD)/asan
	cd $(BUILD)/asan && for f in $(BRAIDS_SRC); do \
	  $(CXX) -g -O1 -w $(STD) $(BRAIDS_WRAPV) $(SAN) $(DEFINES) $(INCLUDES) -c $$f -o $$(basename $$f .cpp).o || exit 1; done
	cd $(BUILD)/asan && $(CXX) -g -O1 -w $(STD) $(SAN) $(DEFINES) $(INCLUDES) -c $(PIGTAIL_SRC) -o PigtailInstrument.o
	cd $(BUILD)/asan && $(CXX) -g -O1 -w $(STD) $(SAN) $(DEFINES) $(INCLUDES) -c $(CURDIR)/pt_lab.cpp -o pt_lab.o
	$(CXX) $(SAN) -o $@ $(BUILD)/asan/*.o $(SAN_OBJS) $(LIBS)

# Bit identity: same flags for both builds; only the Braids sources differ
IDENT_FLAGS := -O1 -w $(STD) -fwrapv

ident:
	@mkdir -p $(BUILD)/pristine
	rm -rf $(BUILD)/pristine/sources
	git -C $(ROOT) archive $(PRISTINE) sources/Externals/Braids | tar -x -C $(BUILD)/pristine
	$(CXX) $(IDENT_FLAGS) -I$(BUILD)/pristine/sources/Externals/Braids -o $(BUILD)/ident_pristine ident.cpp \
	  $(BUILD)/pristine/sources/Externals/Braids/braids/*.cpp $(BUILD)/pristine/sources/Externals/Braids/stmlib/utils/random.cpp
	$(CXX) $(IDENT_FLAGS) -DPATCHED -I$(ROOT)/sources/Externals/Braids -o $(BUILD)/ident_current ident.cpp $(BRAIDS_SRC)
	$(BUILD)/ident_pristine > $(BUILD)/ident_pristine.txt
	$(BUILD)/ident_current > $(BUILD)/ident_current.txt
	@if cmp -s $(BUILD)/ident_pristine.txt $(BUILD)/ident_current.txt; then \
	  echo "ident: 96 kHz output IDENTICAL to $(PRISTINE) for all $$(grep -vc all $(BUILD)/ident_current.txt) models"; \
	else echo "ident: DIFFERS from $(PRISTINE):"; diff $(BUILD)/ident_pristine.txt $(BUILD)/ident_current.txt; exit 1; fi

audit: lab
	$(BUILD)/pt_lab audit 2>/dev/null | grep -v '^\['

tune: lab
	$(BUILD)/pt_lab tune 2>/dev/null | grep -v '^\['

init: lab
	$(BUILD)/pt_lab init 2>/dev/null | grep -v '^\['

cpu: lab
	$(BUILD)/pt_lab cpu 2>/dev/null | grep -v '^\['

stress: asan
	UBSAN_OPTIONS=print_stacktrace=0 timeout 900 $(BUILD)/pt_lab_asan stress 2>&1 | grep -v '^\['

all-tests: ident init audit stress cpu tune

clean:
	rm -rf $(BUILD)
