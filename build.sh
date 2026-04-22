#!/bin/bash

CFLAGS="-Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -Iraylib-5.5_linux_amd64/include"
LFLAGS="-Lraylib-5.5_linux_amd64/lib/ -l:libraylib.a -lGL -lm -lpthread -ldl -lrt -lX11"

if [ $# -eq 0 ] || [ $1 == debug ]; then
    gcc -g -DDEBUG -DAETRIS_LINUX $CFLAGS aetris_linux.c -o aetris_linux $LFLAGS
elif [ $1 == release ]; then
    gcc -O2 -DNDEBUG -DAETRIS_LINUX $CFLAGS aetris_linux.c -o aetris_linux $LFLAGS
    cl gdi32.lib msvcrt.lib raylib.lib winmm.lib user32.lib shell32.lib Ws2_32.lib -MT -Gm- -nologo -Oi -GR- -EHa- -Zi -WX -W4 -wd4201 -wd4100 -wd4189 -wd4200 -wd4244 -DNDEBUG -DAETRIS_WIN32 -Iraylib-5.5_win64_msvc16/include /O2 aetris_win32.c /link /libpath:raylib-5.5_win64_msvc16/lib /NODEFAULTLIB:libcmt /NODEFAULTLIB:msvcrtd -opt:ref
else
	echo Unkown argument!
fi
