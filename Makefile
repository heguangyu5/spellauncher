spellauncher-dev: spellauncher.c
	gcc -g -Wall -DDEV spellauncher.c -o spellauncher-dev -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

spellauncher: spellauncher.c
	gcc -O2 spellauncher.c resources.S -o spellauncher -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
