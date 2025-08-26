PROJECT_NAME := VtkViewer

.PHONY: all build run clean install

all: build

build:
	cmake --preset=default
	cmake --build build

run: build
	cmake --build build --target run

clean:
	rm -rf build

install:
	cmake -DCMAKE_BUILD_TYPE=Release -S . -B build
	cmake --build build --config Release
	cmake --install build --prefix $$HOME/.local