TARGET=3dworld.html
BUILD=obj
VPATH=$(BUILD) src src/texture_tile_blend
CXX=em++ -g -O3 -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
-sFORCE_FILESYSTEM=1  -sFULL_ES2=1 -sFULL_ES3=1 --closure 0 -sGL_TESTING=1 \
$(INCLUDES) $(DEFINES) -Wextra -Wno-unused-parameter -Wno-implicit-fallthrough \
-sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=1400mb
TARGA=Targa
GLI=dependencies/gli
GLM=dependencies/glm
JPEG=dependencies/jpeg-9a
PNG=dependencies/libpng-1.2.20
JPEGt=dependencies/libjpeg-turbo
GLUT=dependencies/freeglut-2.8.1/include 
MESH=dependencies/meshoptimizer/src
ALUT=dependencies/freealut/include
GLEW=dependencies/glew-2.0.0/include
TIFF=dependencies/tiff-4.3.0/libtiff

INCLUDES=-Isrc/texture_tile_blend -I$(TARGA) -I$(GLI) -I$(GLM) -Isrc -I$(GLUT) -I$(MESH) -I$(JPEG) -I$(ALUT) -I$(JPEGt) -I$(PNG) -I$(GLEW) -I$(TIFF)
DEFINES=-DENABLE_JPEG -DENABLE_PNG -DENABLE_TIFF -DENABLE_DDS
CXXFLAGS=

OBJS=$(shell cat obj_list)

LINK=$(CPP) -fopenmp $(INCLUDES)
LDFLAGS=-lpthread `pkg-config --libs zlib libpng libjpeg libtiff-4 xrender glew freealut` 

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
