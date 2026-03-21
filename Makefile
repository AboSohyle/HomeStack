export GCC_COLORS

CXX            := g++
WINDRES        := windres
TARGET_RELEASE := D:/Portable/HomeStack/HomeStack
TARGET_DEBUG   := $(TARGET_RELEASE)-debug
REL_BUILD      := .release
DEB_BUILD      := .debug

# --- Flags Static linking ---
CDEFFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -fdiagnostics-color=always -I. -MMD -MP
LIBS      := -municode -mwindows -lcomctl32 -lpathcch -ldwmapi -lole32 -lwininet -loleaut32 -luuid -lshlwapi -luxtheme
CFLAGS    := -O2 -flto -march=native -ffunction-sections -fdata-sections -fno-ident -DNDEBUG -static -fno-rtti
CDEBFLAGS := -Og -g3 -fno-omit-frame-pointer -D_DEBUG -DDEBUG
LDFLAGS   := -Wl,--gc-sections,--strip-all,--allow-multiple-definition -flto
LDEBFLAGS := -Wl,--allow-multiple-definition

# --- Flags UCRT linking ---
# CDEFFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -fdiagnostics-color=always -I. -MMD -MP -D_UCRT
# LIBS      := -municode -mwindows -lcomctl32 -lpathcch -ldwmapi -lole32 -lwininet -loleaut32 -luuid -lshlwapi -luxtheme
# CFLAGS    := -O2 -flto -march=native -ffunction-sections -fdata-sections -fno-ident -DNDEBUG -fno-rtti
# CDEBFLAGS := -Og -g3 -fno-omit-frame-pointer -D_DEBUG -DDEBUG
# LDFLAGS   := -Wl,--gc-sections,--strip-all -flto -lucrt
# LDEBFLAGS := -Wl -lucrt


# --- Files ---
SRCS     := $(wildcard *.cpp)
REL_OBJS := $(SRCS:%.cpp=$(REL_BUILD)/%.o) $(REL_BUILD)/resource.o
DEB_OBJS := $(SRCS:%.cpp=$(DEB_BUILD)/%.o) $(DEB_BUILD)/resource.o

.PHONY: default release debug clean run

default: release

-include $(REL_OBJS:.o=.d) $(DEB_OBJS:.o=.d)

# --- Release Build ---
release: $(REL_BUILD) $(REL_OBJS)
	$(CXX) $(REL_OBJS) -o "$(TARGET_RELEASE).exe" $(CFLAGS) $(LDFLAGS) $(LIBS)

$(REL_BUILD)/%.o: %.cpp | $(REL_BUILD)
	$(CXX) -c $< -o $@ $(CDEFFLAGS) $(CFLAGS)

$(REL_BUILD)/resource.o: resource.rc | $(REL_BUILD)
	$(WINDRES) -i $< -o $@ -DNDEBUG

# --- Debug Build ---
debug: $(DEB_BUILD) $(DEB_OBJS)
	$(CXX) $(DEB_OBJS) -o "$(TARGET_DEBUG).exe" $(CDEBFLAGS) $(LDEBFLAGS) $(LIBS)

$(DEB_BUILD)/%.o: %.cpp | $(DEB_BUILD)
	$(CXX) -c $< -o $@ $(CDEFFLAGS) $(CDEBFLAGS)

$(DEB_BUILD)/resource.o: resource.rc | $(DEB_BUILD)
	$(WINDRES) -i $< -o $@ -D_DEBUG


$(REL_BUILD) $(DEB_BUILD):
	@if not exist $@ mkdir $@

run: release
	@"$(TARGET_RELEASE).exe"

clean:
	@if exist $(REL_BUILD) rd /s /q $(REL_BUILD)
	@if exist $(DEB_BUILD) rd /s /q $(DEB_BUILD)
	@if exist "$(TARGET_DEBUG).exe" del /q "$(TARGET_DEBUG).exe"
	@if exist "$(subst /,\,$(TARGET_RELEASE).exe)" del /q "$(subst /,\,$(TARGET_RELEASE).exe)"
