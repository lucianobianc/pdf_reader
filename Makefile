# Works with GNU make and FreeBSD make. CMake handles the underlying build.
PREFIX ?= $(HOME)/.local
BUILD_DIR ?= build
BUILD_TYPE ?= Release
BUILD_TESTING ?= ON
JOBS ?= 2
CMAKE ?= cmake
CTEST ?= ctest
CMAKE_ARGS ?=

all: build

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)" -G Ninja \
		-DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		-DCMAKE_INSTALL_PREFIX="$(PREFIX)" \
		-DBUILD_TESTING="$(BUILD_TESTING)" $(CMAKE_ARGS)

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --parallel "$(JOBS)"

test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure

install: build
	DESTDIR="$(DESTDIR)" $(CMAKE) --install "$(BUILD_DIR)" --prefix "$(PREFIX)"

clean:
	@if test -f "$(BUILD_DIR)/CMakeCache.txt"; then \
		$(CMAKE) --build "$(BUILD_DIR)" --target clean; \
	fi

.PHONY: all configure build test install clean
