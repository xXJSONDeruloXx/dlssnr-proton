WINCC ?= x86_64-w64-mingw32-gcc
CC ?= cc
CXX ?= c++
ROCM_ROOT ?= /opt/rocm
ROCM_LIB ?= $(ROCM_ROOT)/lib
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra

.PHONY: all client backend native-probe native-host test release
all: client backend native-probe native-host
client: build/amdhip64_7.dll
backend: build/nr-backend
native-probe: build/nr-native-probe
native-host: build/nr-native-host

build:
	mkdir -p build

build/amdhip64_7.dll: src/client.c src/protocol.h src/signatures.h | build
	$(WINCC) -O2 -std=c11 -Wall -Wextra -shared -static -o $@ $< -lws2_32

build/nr-backend: src/backend.cpp src/protocol.h | build
	$(CXX) $(CXXFLAGS) -I$(ROCM_ROOT)/include -o $@ $< 	-L$(ROCM_LIB) -Wl,-rpath-link,$(ROCM_LIB) $(EXTRA_LDFLAGS) -lamdhip64

build/nr-native-probe: src/native_probe.c src/native_api.c src/native_api.h | build
	$(CC) -O2 -std=c11 -Wall -Wextra -Werror -o $@ src/native_probe.c src/native_api.c -ldl

build/nr-native-host: src/native_host.c src/native_api.c src/native_api.h src/native_protocol.h | build
	$(CC) -O2 -std=c11 -Wall -Wextra -Werror -o $@ src/native_host.c src/native_api.c -ldl

build/probe.exe: tests/probe.c | build
	$(WINCC) -O2 -std=c11 -Wall -Wextra -o $@ $<

test:
	python3 -m unittest discover -s tests -p 'test_*.py'

release: all
	python3 package.py
