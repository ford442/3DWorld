TARGET=3dworld
BUILD=obj
VPATH=$(BUILD) src src/texture_tile_blend
CXX=em++
TARGA=Targa
GLI=dependencies/gli
GLM=dependencies/glm
INCLUDES=-Isrc -Isrc/texture_tile_blend -I$(TARGA) -I$(GLI) -I$(GLM)
DEFINES=-DENABLE_JPEG -DENABLE_PNG -DENABLE_TIFF -DENABLE_DDS
CXXFLAGS=-g -Wall -O3 -fopenmp $(INCLUDES) $(DEFINES) -Wextra -Wno-unused-parameter -Wno-implicit-fallthrough \
OBJS=$(shell cat obj_list)

LINK=$(CPP) -fopenmp $(INCLUDES)
LDFLAGS=-lpthread `pkg-config --libs zlib libpng libjpeg libtiff-4 xrender glew freealut` -sUSE_WEBGL2=1

DEPFLAGS = -MT $@ -MMD -MP -MF $(BUILD)/$*.Td
POSTCOMPILE = mv -f $(BUILD)/$*.Td $(BUILD)/$*.d

ifeq ($(VERBOSE),1)
Q :=
else
Q := @
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $<"
	$(Q)cd $(BUILD) && $(CXX) $(INCLUDES) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o : %.cpp $(BUILD)/%.d
	@echo "Compiling $<"
	$(Q)$(CXX) $(DEPFLAGS) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c $(abspath $<) -o $(abspath $(BUILD)/$@)
	@$(POSTCOMPILE)

.PHONY: clean
clean:
	-rm -fr $(BUILD)

# Create the directory before compiling sources
$(OBJS): | $(BUILD)
$(BUILD):
	@mkdir -p $(BUILD)

# Create the dependency files
$(BUILD)/%.d: ;
.PRECIOUS: $(BUILD)/%.d

-include $(patsubst %,$(BUILD)/%.d,$(basename $(OBJS)))
