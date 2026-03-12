SHELL := /bin/sh

CXX ?= g++
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wextra -pedantic
INCLUDES := -Icpp/include
BIN_DIR := bin
SERVER_BIN := $(BIN_DIR)/sysnetmon-server
CLIENT_BIN := $(BIN_DIR)/sysnetmon-agent

.PHONY: all server client clean dashboard-install dashboard-run run-server run-client docker-build docker-up docker-down cmake-build

all: server client

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

server: $(SERVER_BIN)

client: $(CLIENT_BIN)

$(SERVER_BIN): cpp/server/main.cpp cpp/include/common.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(CLIENT_BIN): cpp/client/main.cpp cpp/include/common.hpp cpp/include/metrics.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

dashboard-install:
	pip install -r python/dashboard/requirements.txt

dashboard-run:
	cd python/dashboard && flask --app app run --host=0.0.0.0 --port=5000

run-server: server
	./$(SERVER_BIN) 9090

run-client: client
	./$(CLIENT_BIN) 127.0.0.1 9090 agent-1 5

docker-build:
	docker compose build

docker-up:
	docker compose up --build

docker-down:
	docker compose down

cmake-build:
	cmake -S . -B build
	cmake --build build --config Release

clean:
	rm -rf $(BIN_DIR) build