
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 \
            -Wno-deprecated-declarations \
            $(shell pkg-config --cflags glib-2.0 gio-2.0)

LDFLAGS  := -lssl -lcrypto -lpthread \
            $(shell pkg-config --libs glib-2.0 gio-2.0)


SRC_DIR  := src
OBJ_DIR  := obj
BIN      := blukeyborg-cli

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(BIN)

.PHONY: all clean
