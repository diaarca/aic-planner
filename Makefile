CXX = clang++

BUILD_DIR = build
SRC_DIR = src
INCLUDE_DIR = include/aic-planner
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

TARGET = $(BIN_DIR)/aic_planner
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# ORTOOLS_DIR and CSV_PARSER_DIR are defined in the flake.nix
CXXFLAGS = -I$(INCLUDE_DIR) -std=c++17 -O3 -DOR_PROTO_DLL= \
		   -I$(ORTOOLS_DIR)/include -I$(CSV_PARSER_DIR)/include
LDFLAGS += -L$(ORTOOLS_DIR)/lib -Wl,-rpath,$(ORTOOLS_DIR)/lib
LDLIBS = -lortools

.PHONY: all clean

all: $(TARGET)

# Rule to link the executable
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR) $(OBJ_DIR): $(BUILD_DIR)
	mkdir -p $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
