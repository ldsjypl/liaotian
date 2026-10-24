CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wno-unused-parameter
LDFLAGS  ?=

SRC := src/main.cpp \
       src/core/server.cpp \
       src/core/http.cpp \
       src/core/admin.cpp \
       src/net/websocket.cpp \
       src/storage/message_log.cpp \
       src/chat/room.cpp \
       src/chat/hub.cpp
OBJ := $(SRC:.cpp=.o)
BIN := bin/liaotian

.PHONY: all clean run install-service

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDFLAGS)

src/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN) --port 8081 --web ./web --data ./data

clean:
	rm -f $(OBJ) $(BIN)

install-service:
	sudo install -m 644 deploy/liaotian.service /etc/systemd/system/liaotian.service
	sudo systemctl daemon-reload
	sudo systemctl enable --now liaotian
