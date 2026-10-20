# Builds the PC screenshot tool (needs g++ and libsdl2-dev).
#   make -C tools/preview M5GFX=path/to/M5GFX/src   (default: .pio/libdeps/core2)
#   make -C tools/preview shots      -> docs/screenshots/*.png
M5GFX ?= ../../.pio/libdeps/core2/M5GFX/src
ROOT := ../..
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

GFX_CPP := $(shell find $(M5GFX) -name '*.cpp')
GFX_C := $(shell find $(M5GFX) -name '*.c')
GFX_OBJ := $(patsubst $(M5GFX)/%,build/gfx/%,$(GFX_CPP:.cpp=.o) $(GFX_C:.c=.o))

APP_SRC := $(wildcard $(ROOT)/src/ui/*.cpp) $(wildcard $(ROOT)/src/ui/apps/*.cpp) \
           $(ROOT)/src/core/civil.cpp $(ROOT)/src/core/tz.cpp preview.cpp
APP_OBJ := $(patsubst $(ROOT)/%,build/app/%,$(filter $(ROOT)/%,$(APP_SRC:.cpp=.o))) build/app/preview.o

CXXFLAGS := -std=c++17 -O2 -MMD -MP -I$(M5GFX) -I$(ROOT)/src $(SDL_CFLAGS)

preview: $(APP_OBJ) build/libm5gfx.a
	$(CXX) -o $@ $(APP_OBJ) build/libm5gfx.a $(SDL_LIBS) -lpthread

build/libm5gfx.a: $(GFX_OBJ)
	ar rcs $@ $^

build/gfx/%.o: $(M5GFX)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 -w -I$(M5GFX) $(SDL_CFLAGS) -c $< -o $@

build/gfx/%.o: $(M5GFX)/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -I$(M5GFX) $(SDL_CFLAGS) -c $< -o $@

build/app/preview.o: preview.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wall -Wextra -c $< -o $@

build/app/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wall -Wextra -Wno-unused-parameter -c $< -o $@

shots: preview
	@mkdir -p $(ROOT)/docs/screenshots
	./preview $(ROOT)/docs/screenshots .

clean:
	rm -rf build preview

-include $(APP_OBJ:.o=.d)

.PHONY: shots clean
