CXX = clang++

CPLEX_DIR = /Applications/CPLEX_Studio2211
ARCH = arm64_osx

SRC_DIR = src
HDR_DIR = hdr
OBJ_DIR = obj
BIN_DIR = bin

TARGET = $(BIN_DIR)/aic_planner
SRCS = $(wildcard src/*.cpp)
OBJS = $(SRCS:src/%.cpp=$(OBJ_DIR)/%.o)


CXXFLAGS = -I"$(CPLEX_DIR)/cplex/include/" -I"$(CPLEX_DIR)/concert/include/" -I$(HDR_DIR) -std=c++11 -Wno-deprecated-declarations
LDFLAGS = -L"$(CPLEX_DIR)/cplex/lib/$(ARCH)/static_pic" -L"$(CPLEX_DIR)/concert/lib/$(ARCH)/static_pic"
LDLIBS = -lilocplex -lconcert -lcplex -lm -lpthread

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
