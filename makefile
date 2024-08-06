TARGET=3dworld.html
BUILD=obj
VPATH=$(BUILD) src src/texture_tile_blend

GL_FLAGS += -sFULL_ES3=1 -sFULL_ES2=0 -sGL_MAX_TEMP_BUFFER_SIZE=4gb -sGL_DEBUG=0 \
-sGL_TRACK_ERRORS=0 -sGL_UNSAFE_OPTS=1 -sUSE_LIBPNG=1 -sUSE_ZLIB=1 \
-sGL_POOL_TEMP_BUFFERS=1 -sGL_ASSERTIONS=0 -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 \
-sMAX_WEBGL_VERSION=2 

TARGA=Targa
GLI=dependencies/gli
GLM=dependencies/glm

INCLUDES=-I/usr/include/GL -Isrc -Isrc/texture_tile_blend -I$(TARGA) -I$(GLI) -I$(GLM) \
-Idependencies/meshoptimizer/src -Idependencies/assimp/include \
-Idependencies/freealut/include -Idependencies/stb

DEFINES=-DENABLE_PNG -DENABLE_TIFF -DENABLE_DDS -DENABLE_STB_IMAGE -DENABLE_ASSIMP

CXXFLAGS=-g -Wall -O3 $(INCLUDES) $(DEFINES) -Wextra -Wno-unused-parameter -Wno-implicit-fallthrough \
#-Wstrict-aliasing=2 -Wunreachable-code -Wcast-align -Wcast-qual -Wsign-compare -Wsign-promo \
-Wdisabled-optimization -Winit-self -Wlogical-op -Wmissing-include-dirs -Wnoexcept -Woverloaded-virtual \
-Wredundant-decls -Wstrict-null-sentinel -Wno-unused -Wno-variadic-macros -Wno-parentheses -fdiagnostics-show-option \
-fasynchronous-unwind-tables -fexceptions -Werror=implicit-function-declaration -pedantic -pedantic-errors -Wformat=2 \
-Wformat-nonliteral -Wformat-security -Wformat-y2k -Wimport -Winvalid-pch -Wmissing-field-initializers \
-Wmissing-format-attribute -Wpacked -Wpointer-arith -Wstack-protector -fstack-protector-strong \
-D_FORTIFY_SOURCE=2 -Wunused -Wvariadic-macros -Wwrite-strings -Werror=return-type -D_GLIBCXX_ASSERTIONS \
-fexceptions -fasynchronous-unwind-tables -Wctor-dtor-privacy -Wnon-virtual-dtor 

OBJS=$(shell cat obj_list)

LINK=$(CPP) $(INCLUDES)
LDFLAGS=$(GL_FLAGS) -lpthread -lopenal `pkg-config --libs zlib libpng libtiff-4 xrender glew freealut` -lglut -lassimp 

# For creating dependencies files
DEPFLAGS = -MT $@ -MMD -MP -MF $(BUILD)/$*.Td
POSTCOMPILE = mv -f $(BUILD)/$*.Td $(BUILD)/$*.d

# Change the verbosity of the makefile.
ifeq ($(VERBOSE),1)
Q :=
else
Q := @
endif

# Compile 3dworld
all: $(TARGET)

# Link the target
$(TARGET): $(OBJS)
	@echo "Linking $<"
	$(Q)cd $(BUILD) && $(CXX) $(INCLUDES) -o $(TARGET) $(OBJS) $(LDFLAGS)

# Compile source files
%.o : %.cpp $(BUILD)/%.d
	@echo "Compiling $<"
	$(Q)$(CXX) $(DEPFLAGS) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c $(abspath $<) -o $(abspath $(BUILD)/$@)
	@$(POSTCOMPILE)

# Delete compiled files
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
