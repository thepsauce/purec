# Compiler flags
DEBUG_FLAGS := -g -fsanitize=address -Werror -pg
C_FLAGS := -Isrc -std=gnu99 -Wall -Wextra -Wpedantic
RELEASE_FLAGS := -O3

# Libraries
C_LIBS := -lncursesw -lX11

# Input
SRC := src
TESTS := tests

# Output
RUN := purec
OUT := build
RELEASE := release

$(shell mkdir -p $(OUT))
$(shell mkdir -p $(OUT)/$(TESTS))

MAIN_SOURCE := $(SRC)/main.c
MAIN_OBJECT := $(OUT)/main.o
# Find all source files and test files
SOURCES := $(filter-out $(MAIN_SOURCE),$(shell find $(SRC) -name '*.c'))
# Get all corresponding object paths
OBJECTS := $(patsubst $(SRC)/%.c,$(OUT)/%.o,$(SOURCES))

# Get test sources and main sources
TEST_MAIN_SOURCES=$(TESTS)/buf.c $(TESTS)/rel.c $(TESTS)/main.c
TEST_SOURCES=$(TESTS)/test.c
# Get the corresponding test object paths
TEST_OBJECTS := $(patsubst %.c,$(OUT)/%.o,$(TEST_SOURCES))
TEST_BINARIES := $(patsubst %.c,$(OUT)/%,$(TEST_MAIN_SOURCES))

# Get dependencies
DEPENDENCIES := $(patsubst %.o,%.d,$(OBJECTS))

.PHONY: default
default: build

# Include dependencies (.d files) generated by gcc
-include $(DEPENDENCIES)
# Build each object from corresponding source file
$(OUT)/%.o: $(SRC)/%.c
	gcc $(DEBUG_FLAGS) $(C_FLAGS) -c $< -o $@ -MMD

$(OUT)/$(TESTS)/%.o: $(TESTS)/%.c
	gcc $(DEBUG_FLAGS) $(C_FLAGS) -c $< -o $@ -MMD

# Build the main executable from all object files
$(OUT)/$(RUN): $(OBJECTS) $(MAIN_OBJECT)
	gcc $(DEBUG_FLAGS) $(C_FLAGS) $(OBJECTS) $(MAIN_OBJECT) -o $@ $(C_LIBS)

# Build the test executables
$(OUT)/$(TESTS)/%: $(OBJECTS) $(TEST_OBJECTS) $(OUT)/$(TESTS)/%.o $(TESTS)/test.h
	gcc $(DEBUG_FLAGS) $(C_FLAGS) $(OBJECTS) $(TEST_OBJECTS) $(@:$(OUT)/%=%).c -o $@ $(C_LIBS)

# Functions
.PHONY: build run gdb test release clean

build: $(OUT)/$(RUN)

run: build
	./$(OUT)/$(RUN) 2>asan.txt

gdb: build
	gdb $(OUT)/$(RUN)

test: build $(TEST_BINARIES)
	./$(OUT)/$(TESTS)/$(t) 2>asan.txt

release:
	mkdir -p $(RELEASE)
	gcc $(RELEASE_FLAGS) $(C_FLAGS) $(SOURCES) $(MAIN_SOURCE) -o $(RELEASE)/$(RUN) $(C_LIBS)

clean:
	rm -rf $(OUT) $(RELEASE)
