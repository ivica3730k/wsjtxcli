# Build entry points. Every target drops its binary in ./binaries (gitignored)
# so CI can collect it without knowing the generator's layout.

SHELL        := /bin/bash
BIN_DIR      := binaries
BUILD_DIR    := build
CMAKE_FLAGS  ?= -DCMAKE_BUILD_TYPE=Release
JOBS         ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
PREFIX       ?= /usr/local

.PHONY: help build test install clean distclean

help:
	@echo "Targets:"
	@echo "  build      build wsjtxcli for the host"
	@echo "  test       configure, build and run the Catch2 suite"
	@echo "  install    install the binary under PREFIX (default /usr/local)"
	@echo "  clean      remove build trees, keep binaries"
	@echo "  distclean  remove build trees and binaries"

build:
	cmake -S . -B $(BUILD_DIR)/host $(CMAKE_FLAGS) -DWSJTXCLI_BUILD_TESTS=OFF \
	      -DWSJTXCLI_BIN_DIR=$(CURDIR)/$(BIN_DIR)/host
	cmake --build $(BUILD_DIR)/host -j $(JOBS)

test:
	cmake -S . -B $(BUILD_DIR)/test $(CMAKE_FLAGS) -DWSJTXCLI_BUILD_TESTS=ON \
	      -DWSJTXCLI_BIN_DIR=$(CURDIR)/$(BIN_DIR)/test
	cmake --build $(BUILD_DIR)/test -j $(JOBS)
	ctest --test-dir $(BUILD_DIR)/test --output-on-failure

install: build
	install -d $(PREFIX)/bin
	install -m 0755 $(BIN_DIR)/host/wsjtxcli $(PREFIX)/bin/wsjtxcli

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf $(BIN_DIR) dist
