# slot_lab: load/save harness for instrument slots and project files.
# Not part of the default build. See docs/design/instrument-slots.md.
#
# The same slot_lab.cpp is built once per tree, linked against that tree's
# objects, so each binary uses that tree's own loader and save code:
#   fork      this repository's projects/buildX64. Build it first:
#             cd projects && make PLATFORM=X64
#   upstream  djdiskmachine $(UPSTREAM), exported to build/upstream and built
#             with its own X64 Makefile (~40 s the first time)
#
# From tools/slot_lab:
#   make check       fork dumps and resaves of every fixture, diffed against
#                    baseline/$(REF) (default REF=fork; REF=upstream compares
#                    with upstream's loader). Exits 1 on any difference
#   make compare     fork dumps and resaves of every fixture against
#                    upstream's (baseline/upstream), by compare.py: upstream's
#                    sampleless Samples read as empty slots, top-level node
#                    order and LAYOUT ignored. Exits 1 on any difference
#   make mem         heap of each fixture's project in both trees (slot_lab mem)
#   make baseline    regenerate baseline/<tree>/ for BASELINE_TREES (default
#                    both) and check that each tree's resave loads back
#                    to the same dump. baseline/fork is the reference for
#                    stages that must not change behaviour: regenerate it on
#                    purpose only (make baseline BASELINE_TREES=upstream,
#                    or make baseline-upstream)
#   make fixtures    regenerate the generated and hand-made fixtures. Changes
#                    the inputs: only when a fixture's definition changes
#   make trees       build both binaries
#   make cmdlists    check that the command lists the command popup reads
#                    (each instrument type's, the player's phrase list and
#                    the table player's) match the switches they describe
#                    (cmdlists.py). Exits 1 on any difference
#   make play        play scenarios (slot_lab play) on fixtures/up-basic
#                    (flows and unknown on a copy of it, since they write
#                    into the project),
#                    then a dump of every fixture, under ASan + UBSan: the
#                    sources in SAN_DIRS rebuilt instrumented, the rest linked
#                    from the fork's X64 objects. Exits 1 on any sanitizer report
#
# Per fixture and tree: <name>.dump (slot_lab dump) and <name>.resave.xml
# (the fixture loaded and saved again by that tree). Dump lines starting
# with '#' name the tree and are ignored when comparing.
#
# Fixtures: fixtures/*/ (see fixtures/README.md), plus every lgpt_* project
# of djdiskmachine/lgpt-resources in $(RESOURCES) as res-<project>. Clone
# that outside this repo (design doc section 11):
#   git clone https://github.com/djdiskmachine/lgpt-resources ~/lgpt-resources
#   git -C ~/lgpt-resources checkout 9624b43
# Their dumps are committed; their resaves (the projects themselves, saved
# again) stay local and untracked.

ROOT     := $(abspath ../..)
BUILD    := build
UPSTREAM := c7258ff
REF      ?= fork
BASELINE_TREES ?= upstream fork
RESOURCES ?= $(HOME)/lgpt-resources
RES_PIN  := 9624b43
SHELL    := /bin/bash

CXX      ?= g++
DEFINES  := -DPLATFORM_X64 -D_64BIT -DCPP_MEMORY -D__LINUX_ALSA__ -D__LINUX_ALSASEQ__ \
            -DSDL2 -DSDLAUDIO -DRTMIDI -DFFMPEG_ENABLED -D_FEAT_MIDI_LOCK
# InstrumentView.h (included for the fork) hides a View overload
CXXFLAGS := -g -O0 -Wall -Wno-overloaded-virtual -std=gnu++03
LIBS     := $(shell pkg-config alsa --libs) $(shell pkg-config jack --libs 2>/dev/null) \
            $(shell pkg-config sdl2 --libs) -lpthread
SDL_INC  := $(shell pkg-config sdl2 --cflags)

FORK_OBJDIR := $(ROOT)/projects/buildX64
UP_TREE     := $(BUILD)/upstream

FIXTURES := $(sort $(patsubst fixtures/%/lgptsav.dat,%,$(wildcard fixtures/*/lgptsav.dat)))
RES_PROJ := $(sort $(patsubst $(RESOURCES)/%/lgptsav.dat,%,$(wildcard $(RESOURCES)/lgpt_*/lgptsav.dat)))
# name=directory pairs
ALL      := $(foreach f,$(FIXTURES),$(f)=$(CURDIR)/fixtures/$(f)) \
            $(foreach p,$(RES_PROJ),res-$(p)=$(RESOURCES)/$(p))
TREES    := fork upstream

# ASan build for `make play`: sources in SAN_DIRS that the X64 build has an
# object for are rebuilt instrumented and replace those objects. One UBSan
# check is off because of a known report in code these scenarios don't test
# (design doc, Appendix B): signed-integer-overflow, since
# SampleInstrument::Render computes a pointer from renderParams::feedbackOut_,
# which stays uninitialised while feedback is off
SAN      := -fsanitize=address,undefined -fno-sanitize=signed-integer-overflow \
            -fno-sanitize-recover=address
SAN_DIRS := Application/Instruments Application/Player Application/Mixer Application/Views \
            Application/Views/BaseClasses Application/Persistency Foundation Externals/TinyXML
SAN_SRC   = $(foreach f,$(wildcard $(foreach d,$(SAN_DIRS),$(ROOT)/sources/$(d)/*.cpp)), \
              $(if $(wildcard $(FORK_OBJDIR)/$(notdir $(f:.cpp=.o))),$(f)))
SAN_SKIP  = LINUXMain.o $(notdir $(SAN_SRC:.cpp=.o))
PLAY     := range clone view dlay flows types resolve fields unknown

.PHONY: trees check compare mem baseline $(addprefix baseline-,$(TREES)) fixtures check-objs clean play cmdlists

trees: $(addprefix $(BUILD)/slot_lab_,$(TREES))

# ------------------------------------------------------------- binaries

check-objs:
	@test -f $(FORK_OBJDIR)/LINUXMain.o || { echo "slot_lab: no X64 objects in $(FORK_OBJDIR); run 'cd projects && make PLATFORM=X64' first"; exit 1; }

$(BUILD)/slot_lab_fork: slot_lab.cpp check-objs
	@mkdir -p $(BUILD)/fork
	$(CXX) $(CXXFLAGS) $(DEFINES) $(SDL_INC) -DSL_FORK -DSL_TREE='"fork $(shell git -C $(ROOT) rev-parse --short HEAD)"' \
	  -I$(ROOT)/sources -c slot_lab.cpp -o $(BUILD)/fork/slot_lab.o
	$(CXX) $(CXXFLAGS) -w $(DEFINES) -I$(ROOT)/sources -c $(ROOT)/sources/Adapters/Dummy/Midi/DummyMidi.cpp -o $(BUILD)/fork/DummyMidi.o
	@echo "link $@"
	@$(CXX) -o $@ $(BUILD)/fork/slot_lab.o $(BUILD)/fork/DummyMidi.o \
	  $(filter-out $(FORK_OBJDIR)/LINUXMain.o,$(wildcard $(FORK_OBJDIR)/*.o)) $(LIBS)

$(UP_TREE)/.built:
	rm -rf $(UP_TREE) && mkdir -p $(UP_TREE)
	git -C $(ROOT) archive $(UPSTREAM) | tar -x -C $(UP_TREE)
	cd $(UP_TREE)/projects && $(MAKE) PLATFORM=X64 > ../build.log 2>&1 || { tail -20 ../build.log; exit 1; }
	touch $@

$(BUILD)/slot_lab_upstream: slot_lab.cpp $(UP_TREE)/.built
	$(CXX) $(CXXFLAGS) $(DEFINES) $(SDL_INC) -DSL_UPSTREAM -DSL_TREE='"upstream $(UPSTREAM)"' \
	  -I$(UP_TREE)/sources -c slot_lab.cpp -o $(UP_TREE)/slot_lab.o
	$(CXX) $(CXXFLAGS) -w $(DEFINES) -I$(UP_TREE)/sources -c $(UP_TREE)/sources/Adapters/Dummy/Midi/DummyMidi.cpp -o $(UP_TREE)/DummyMidi.o
	@echo "link $@"
	@$(CXX) -o $@ $(UP_TREE)/slot_lab.o $(UP_TREE)/DummyMidi.o \
	  $(filter-out %/LINUXMain.o,$(wildcard $(UP_TREE)/projects/buildX64/*.o)) $(LIBS)

$(BUILD)/slot_lab_asan: slot_lab.cpp check-objs
	@rm -rf $(BUILD)/asan && mkdir -p $(BUILD)/asan
	@echo "compile $(words $(SAN_SRC)) app sources with ASan + UBSan"
	@cd $(BUILD)/asan && printf '%s\n' $(SAN_SRC) $(ROOT)/sources/Adapters/Dummy/Midi/DummyMidi.cpp | \
	  xargs -P$(shell nproc) -n1 sh -c '$(CXX) -g -O1 -w -std=gnu++03 $(SAN) $(DEFINES) $(SDL_INC) \
	    -I$(ROOT)/sources -I$(ROOT)/sources/Externals/Braids -c "$$0" -o "$$(basename "$$0" .cpp).o"'
	$(CXX) -g -O1 -Wall -Wno-overloaded-virtual -std=gnu++03 $(SAN) $(DEFINES) $(SDL_INC) -DSL_FORK \
	  -DSL_TREE='"fork $(shell git -C $(ROOT) rev-parse --short HEAD) asan"' \
	  -I$(ROOT)/sources -c slot_lab.cpp -o $(BUILD)/asan/slot_lab.o
	@echo "link $@"
	@$(CXX) $(SAN) -o $@ $(BUILD)/asan/*.o \
	  $(filter-out $(addprefix $(FORK_OBJDIR)/,$(SAN_SKIP)),$(wildcard $(FORK_OBJDIR)/*.o)) $(LIBS)

# Output in $(BUILD)/play/<scenario>.out, trace and sanitizer output in .log
play: $(BUILD)/slot_lab_asan
	@rm -rf $(BUILD)/play && mkdir -p $(BUILD)/play
	@status=0; for s in $(PLAY); do \
	  dir=fixtures/up-basic; \
	  if [ $$s = flows ] || [ $$s = unknown ]; then dir=$(BUILD)/play/$$s-project; cp -r fixtures/up-basic $$dir; fi; \
	  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
	    $(BUILD)/slot_lab_asan play $$dir $$s > $(BUILD)/play/$$s.out 2> $(BUILD)/play/$$s.log; \
	  rc=$$?; cat $(BUILD)/play/$$s.out; \
	  if [ $$rc != 0 ] || grep -q "ERROR: AddressSanitizer\|runtime error" $(BUILD)/play/$$s.log; then \
	    echo "play $$s: FAILED (exit $$rc)"; grep -m1 -A12 "ERROR: AddressSanitizer\|runtime error" $(BUILD)/play/$$s.log; status=1; \
	  else echo "play $$s: no sanitizer reports"; fi; \
	done; \
	n=0; for fp in $(ALL); do f=$${fp%%=*}; p=$${fp#*=}; n=$$((n+1)); \
	  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
	    $(BUILD)/slot_lab_asan dump $$p > /dev/null 2> $(BUILD)/play/load-$$f.log; rc=$$?; \
	  if [ $$rc != 0 ] || grep -q "ERROR: AddressSanitizer\|runtime error" $(BUILD)/play/load-$$f.log; then \
	    echo "load $$f: FAILED (exit $$rc)"; grep -m1 -A12 "ERROR: AddressSanitizer\|runtime error" $(BUILD)/play/load-$$f.log; status=1; fi; \
	done; echo "loaded $$n fixtures under ASan"; exit $$status

# ------------------------------------------------------------- runs

# $(call run,tree,outdir): dump and resave every fixture; then dump each
# resave (next to a link to the fixture's samples) and compare with the
# first dump, which shows whether the tree's save and load agree
define run
	@if [ -z "$(RES_PROJ)" ]; then echo "note: no projects in $(RESOURCES), res-* fixtures skipped"; \
	elif [ "$$(git -C $(RESOURCES) rev-parse --short=7 HEAD 2>/dev/null)" != $(RES_PIN) ]; then \
	  echo "warning: $(RESOURCES) isn't at $(RES_PIN)"; fi
	@rm -rf $(2) && mkdir -p $(2)
	@for fp in $(ALL); do f=$${fp%%=*}; p=$${fp#*=}; \
	  $(BUILD)/slot_lab_$(1) dump $$p > $(2)/$$f.dump 2> $(2)/$$f.log || { echo "$(1) $$f: dump failed"; exit 1; }; \
	  rm -rf $(BUILD)/resave/$(1)/$$f && mkdir -p $(BUILD)/resave/$(1)/$$f; \
	  ln -s $$p/samples $(BUILD)/resave/$(1)/$$f/samples; \
	  $(BUILD)/slot_lab_$(1) save $$p $(BUILD)/resave/$(1)/$$f 2>> $(2)/$$f.log || { echo "$(1) $$f: save failed"; exit 1; }; \
	  cp $(BUILD)/resave/$(1)/$$f/lgptsav.dat $(2)/$$f.resave.xml; \
	  $(BUILD)/slot_lab_$(1) dump $(BUILD)/resave/$(1)/$$f 2>/dev/null | grep -v '^#' > $(BUILD)/resave/$(1)/$$f.dump; \
	  if grep -v '^#' $(2)/$$f.dump | cmp -s - $(BUILD)/resave/$(1)/$$f.dump; \
	    then echo "$(1) $$f: resave loads back the same"; \
	    else echo "$(1) $$f: RESAVE LOADS BACK DIFFERENTLY"; fi; \
	done
endef

baseline: $(addprefix baseline-,$(BASELINE_TREES))

$(addprefix baseline-,$(TREES)): baseline-%: $(BUILD)/slot_lab_%
	$(call run,$*,baseline/$*)
	@rm -f baseline/$*/*.log

# A missing baseline file counts as a difference
check: $(BUILD)/slot_lab_fork
	$(call run,fork,$(BUILD)/check)
	@status=0; for fp in $(ALL); do f=$${fp%%=*}; \
	  if diff -u <(grep -v '^#' baseline/$(REF)/$$f.dump) <(grep -v '^#' $(BUILD)/check/$$f.dump) > $(BUILD)/check/$$f.dump.diff; \
	    then d="dump same"; else d="DUMP DIFFERS"; status=1; fi; \
	  if diff -u baseline/$(REF)/$$f.resave.xml $(BUILD)/check/$$f.resave.xml > $(BUILD)/check/$$f.resave.diff; \
	    then r="resave same"; else r="RESAVE DIFFERS"; status=1; fi; \
	  echo "vs $(REF) $$f: $$d, $$r"; \
	done; \
	[ $$status = 0 ] || echo "diffs in $(BUILD)/check/*.diff"; exit $$status

compare: $(BUILD)/slot_lab_fork
	$(call run,fork,$(BUILD)/compare)
	@status=0; for fp in $(ALL); do f=$${fp%%=*}; \
	  if python3 -I compare.py dump baseline/upstream/$$f.dump $(BUILD)/compare/$$f.dump > $(BUILD)/compare/$$f.dump.diff; \
	    then d="dump same"; else d="DUMP DIFFERS"; status=1; fi; \
	  if python3 -I compare.py xml baseline/upstream/$$f.resave.xml $(BUILD)/compare/$$f.resave.xml > $(BUILD)/compare/$$f.resave.diff; \
	    then r="resave same"; else r="RESAVE DIFFERS"; status=1; fi; \
	  echo "vs upstream $$f: $$d, $$r"; \
	done; \
	[ $$status = 0 ] || echo "diffs in $(BUILD)/compare/*.diff"; exit $$status

mem: $(addprefix $(BUILD)/slot_lab_,$(TREES))
	@for t in $(TREES); do for fp in $(ALL); do f=$${fp%%=*}; p=$${fp#*=}; \
	  printf "%-9s %-34s " $$t $$f; $(BUILD)/slot_lab_$$t mem $$p 2>/dev/null | grep "^project after"; \
	done; done

cmdlists:
	@python3 -I cmdlists.py $(ROOT)/sources

# ------------------------------------------------------------- fixtures

GEN_UP := basic clones tables

fixtures: $(BUILD)/slot_lab_upstream
	mkdir -p fixtures
	for n in $(GEN_UP); do rm -rf fixtures/up-$$n; \
	  $(BUILD)/slot_lab_upstream gen $$n fixtures/up-$$n 2> $(BUILD)/gen-up-$$n.log || { tail $(BUILD)/gen-up-$$n.log; exit 1; }; done
	python3 -I handmade.py fixtures
	rm -rf fixtures/hand-lz; mkdir -p fixtures/hand-lz; cp -r fixtures/up-basic/samples fixtures/hand-lz/
	$(BUILD)/slot_lab_upstream lz fixtures/up-basic/lgptsav.dat fixtures/hand-lz/lgptsav.dat
	rm -rf fixtures/hand-midi-lz; mkdir -p fixtures/hand-midi-lz; cp -r fixtures/owner-midi/samples fixtures/hand-midi-lz/
	$(BUILD)/slot_lab_upstream lz fixtures/owner-midi/lgptsav.dat fixtures/hand-midi-lz/lgptsav.dat

clean:
	rm -rf $(BUILD)
