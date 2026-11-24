CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow
INC      := -Iinclude
BUILD    := build

LIB_SRC := src/algorithms/myers.cpp src/algorithms/text_utils.cpp \
           src/services/text_diff.cpp src/services/inline_diff.cpp src/services/unified_writer.cpp \
           src/services/html_report.cpp src/services/folder_compare.cpp
LIB_OBJ := $(LIB_SRC:%.cpp=$(BUILD)/%.o)

.PHONY: all test check demo sanitize clean
all: $(BUILD)/redline $(BUILD)/redline_tests

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INC) -MMD -MP -c $< -o $@

$(BUILD)/redline: $(LIB_OBJ) $(BUILD)/src/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/redline_tests: $(LIB_OBJ) $(BUILD)/tests/test_redline.o
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(BUILD)/redline_tests
	./$(BUILD)/redline_tests

# random files and folders, checked against an independent Python implementation
check: $(BUILD)/redline
	python3 tools/verify.py --rounds 300

demo: $(BUILD)/redline
	./$(BUILD)/redline examples/old.cpp examples/new.cpp --html $(BUILD)/file_report.html || true
	./$(BUILD)/redline examples/v1 examples/v2 --html $(BUILD)/folder_report.html || true

sanitize:
	$(MAKE) BUILD=build/asan CXXFLAGS="-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra" test

clean:
	rm -rf build

-include $(LIB_OBJ:.o=.d)
