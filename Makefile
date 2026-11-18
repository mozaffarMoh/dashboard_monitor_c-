BUILD_DIR ?= build

.PHONY: all configure build run once clean

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release

build: configure
	cmake --build $(BUILD_DIR) -j

run: build
	./$(BUILD_DIR)/sysmon

once: build
	./$(BUILD_DIR)/sysmon --once

clean:
	rm -rf $(BUILD_DIR)
