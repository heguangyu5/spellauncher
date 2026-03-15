spellauncher-dev: spellauncher.c
	gcc -g -Wall -DDEV spellauncher.c -o spellauncher-dev -L./lib/linux-x64 -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

spellauncher: spellauncher.c
	gcc -O2 spellauncher.c resources.S -o spellauncher -L./lib/linux-x64 -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

spellauncher-win64: spellauncher.c
	x86_64-w64-mingw32-gcc -O2 spellauncher.c resources.S -o spellauncher.exe -L./lib/mingw-w64 -lraylib -lopengl32 -lgdi32 -lwinmm -lshell32 -static -Wl,--subsystem,windows
