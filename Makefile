# Mars Station build
#   make web      -> build/web/index.html (+ .js/.wasm)   requires emsdk activated
#   make windows  -> build/windows/MarsStation.exe          requires mingw-w64
#   make linux    -> build/linux/mars-station               requires raylib built for desktop
RAYLIB_SRC ?= third_party/raylib/src
SRC = src/main.cpp
CXXFLAGS = -std=c++17 -O2 -Wall -Wno-missing-braces -I$(RAYLIB_SRC)

.PHONY: all web windows linux clean
all: web windows

web: $(SRC) web/shell.html
	mkdir -p build/web
	em++ $(SRC) $(CXXFLAGS) -DPLATFORM_WEB libs/web/libraylib.a -o build/web/index.html \
	  -sUSE_GLFW=3 -sASYNCIFY=0 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 \
	  -sENVIRONMENT=web -sEXPORTED_RUNTIME_METHODS=requestFullscreen,HEAPF32 \
	  --shell-file web/shell.html

windows: $(SRC)
	mkdir -p build/windows
	x86_64-w64-mingw32-windres assets/icon.rc -O coff -o build/windows/icon.o
	x86_64-w64-mingw32-g++ $(SRC) build/windows/icon.o $(CXXFLAGS) libs/win/libraylib.a -o build/windows/MarsStation.exe \
	  -static -static-libgcc -static-libstdc++ -lopengl32 -lgdi32 -lwinmm -mwindows

linux: $(SRC)
	mkdir -p build/linux
	g++ $(SRC) $(CXXFLAGS) libs/linux/libraylib.a -o build/linux/mars-station -lGL -lm -lpthread -ldl -lrt -lX11

clean:
	rm -rf build
