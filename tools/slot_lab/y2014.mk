# Builds the 2014 import (c712f0a) into build/y2014/liby2014.a for slot_lab.
# Called by Makefile after the export; not meant to be run directly.
#
# c712f0a has no X64 target and its Linux builds need SDL 1.2, so this
# compiles its COMMONFILES (projects/Makefile in c712f0a, found through its
# COMMONDIRS in the same order) plus the Unix file system, Unix processes and
# the dummy MIDI service. slot_lab links the archive, so only what it needs is
# pulled in. Two build-only shims, applied by Makefile to the export:
#   shim/SDL/SDL.h includes SDL2's SDL.h (only mutex and timer calls reach it)
#   Externals/Soundfont/DATATYPE.H: the "long is out of range" checks are
#     removed. The SoundFont code assumes 32-bit long and only runs for .sf2
#     files; slot_lab refuses to run this build on a pool with one.

TREE ?= build/y2014
OBJ  := $(TREE)/obj

DIRS := System/Console System/io Services/Midi Services/Time Services/Controllers \
        System/FileSystem System/Timer System/Errors UIFramework/Framework \
        UIFramework/BasicDatas UIFramework/SimpleBaseClasses UIFramework/Interfaces \
        Application Application/Controllers Application/Commands Application/Audio \
        Application/Views Application/Views/ModalDialogs Application/Views/BaseClasses \
        Application/Model Application/Utils Application/Player Application/Mixer \
        Application/Instruments Application/Persistency Foundation Services/Audio \
        Foundation/Services Foundation/Variables Externals/Compression \
        Externals/Soundfont Externals/TinyXML \
        Adapters/Unix/FileSystem Adapters/Unix/Process System/Process Adapters/Dummy/Midi
SRCDIRS := $(addprefix $(TREE)/sources/,$(DIRS))

# COMMONFILES of c712f0a, then the extras
FILES := Logger Trace Result Status Config Timer FileSystem SysMutex TimeService \
         MidiOutDevice MidiInDevice MidiService Groove MidiChannel MidiInMerger \
         GUIEvent GUIRect EventManager GUIWindow Channel Mixer ControlRoom \
         MultiChannelAdapter CommandDispatcher ApplicationCommandDispatcher \
         EventDispatcher ControllerService ButtonControllerSource \
         KeyboardControllerSource JoystickControllerSource HatControllerSource \
         ControllerSource ControlNode I_GUIWindowImp Application AppWindow \
         SelectProjectDialog NewProjectDialog ImportSampleDialog MixerView NullView \
         SongView ChainView PhraseView ViewData ConsoleView ProjectView InstrumentView \
         UIController View ModalView FieldView UIField UIIntField UIIntVarOffField \
         UIIntVarField ViewEvent I_Action UITempoField UIActionField MessageBox \
         GrooveView UINoteVarField UIBigHexVarField SRPUpdaters UIStaticField \
         Song Chain Phrase Project char n_assert fixed wildcard SyncMaster \
         TablePlayback Player Table TableView InstrumentBank WavFileWriter WavFile \
         MidiInstrument Filters SampleVariable SampleInstrument SamplePool CommandList \
         PersistencyService Persistent Observable SingletonRegistry Audio AudioMixer \
         AudioOutDriver AudioDriver AudioOut DummyAudioOut PlayerChannel \
         AudioFileStreamer MixBus MixerService PlayerMixer Service ServiceRegistry \
         SubService Variable VariableContainer WatchedVariable SoundFontPreset \
         SoundFontManager SoundFontSample ENAB HYDRA OMEGA RIFF SFDETECT SFLOOKUP \
         SFNAV SFREADER WIN_MEM HexBuffers lz tinyxmlparser tinyxml tinyxmlerror \
         tinystr Tiny2NosStub \
         UnixFileSystem Process UnixProcess DummyMidi
OBJS := $(addprefix $(OBJ)/,$(addsuffix .o,$(FILES)))

vpath %.cpp $(SRCDIRS)
vpath %.CPP $(SRCDIRS)
vpath %.c   $(SRCDIRS)

FLAGS := -g -O0 -w -DCPP_MEMORY -D_64BIT -I$(TREE)/shim $(shell pkg-config sdl2 --cflags) -I$(TREE)/sources

$(TREE)/liby2014.a: $(OBJS)
	rm -f $@
	ar rcs $@ $(OBJS)

$(OBJ)/%.o: %.cpp | $(OBJ)
	$(CXX) $(FLAGS) -fpermissive -c $< -o $@
$(OBJ)/%.o: %.CPP | $(OBJ)
	$(CXX) $(FLAGS) -fpermissive -x c++ -c $< -o $@
$(OBJ)/%.o: %.c | $(OBJ)
	$(CC) $(FLAGS) -c $< -o $@

$(OBJ):
	mkdir -p $@
