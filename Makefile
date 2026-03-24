CXX = clang++

ORTOOLS_DIR = $(HOME)/OR-TOOLS/or-tools_arm64_macOS-15.3.1_cpp_v9.12.4544

SRC_DIR = src
HDR_DIR = hdr
OBJ_DIR = obj
BIN_DIR = bin

TARGET = $(BIN_DIR)/aic_planner
SRCS = $(wildcard src/*.cpp)
OBJS = $(SRCS:src/%.cpp=$(OBJ_DIR)/%.o)

CXXFLAGS = -I$(ORTOOLS_DIR)/include -I$(HDR_DIR) -std=c++17 -O3 -DOR_PROTO_DLL=
LDFLAGS = -L$(ORTOOLS_DIR)/lib -Wl,-rpath,$(ORTOOLS_DIR)/lib
LDLIBS = -lortools

.PHONY: all clean

all: $(TARGET)

# Rule to link the executable
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LDLIBS)

# Rule to compile source files into the obj directory
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule to create output directories
$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

# Rule to clean all build artifacts
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
