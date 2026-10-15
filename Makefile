# Build entry points. Every target drops its binary in ./binaries (gitignored)
# so CI can collect it without knowing the generator's layout.

SHELL        := /bin/bash
BIN_DIR      := binaries
BUILD_DIR    := build
CMAKE_FLAGS  ?= -DCMAKE_BUILD_TYPE=Release
JOBS         ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
PREFIX       ?= /usr/local

.PHONY: help build build-mac-universal test install clean distclean

help:
	@echo "Targets:"
	@echo "  build      build wsjtxcli for the host"
	@echo "  build-mac-universal  macOS arm64 + x86_64 in one file (what releases ship)"
	@echo "  test       configure, build and run the Catch2 suite"
	@echo "  install    install the binary under PREFIX (default /usr/local)"
	@echo "  clean      remove build trees, keep binaries"
	@echo "  distclean  remove build trees and binaries"

build:
	cmake -S . -B $(BUILD_DIR)/host $(CMAKE_FLAGS) -DWSJTXCLI_BUILD_TESTS=OFF \
	      -DWSJTXCLI_BIN_DIR=$(CURDIR)/$(BIN_DIR)/host
	cmake --build $(BUILD_DIR)/host -j $(JOBS)

# One file carrying both slices, so macOS users have a single download
# whatever their hardware. Apple's SDK ships both, so either machine builds it.
build-mac-universal:
	cmake -S . -B $(BUILD_DIR)/mac-universal $(CMAKE_FLAGS) -DWSJTXCLI_BUILD_TESTS=OFF \
	      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
	      -DWSJTXCLI_BIN_DIR=$(CURDIR)/$(BIN_DIR)/macos-universal
	cmake --build $(BUILD_DIR)/mac-universal -j $(JOBS)

test:
	cmake -S . -B $(BUILD_DIR)/test $(CMAKE_FLAGS) -DWSJTXCLI_BUILD_TESTS=ON \
	      -DWSJTXCLI_BIN_DIR=$(CURDIR)/$(BIN_DIR)/test
	cmake --build $(BUILD_DIR)/test -j $(JOBS)
	cd $(BUILD_DIR)/test && ctest --output-on-failure

install: build
	install -d $(PREFIX)/bin $(PREFIX)/share/doc/wsjtxcli
	install -m 0755 $(BIN_DIR)/host/wsjtxcli $(PREFIX)/bin/wsjtxcli
	install -m 0644 LICENSE THIRD-PARTY-NOTICES.md $(PREFIX)/share/doc/wsjtxcli/

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf $(BIN_DIR) dist
