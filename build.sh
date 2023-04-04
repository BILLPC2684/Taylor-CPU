gcc ./taylor.c -O0 -o ./taylor.o -g -lm -pthread \
-Wformat-overflow=0 -Wno-int-conversion \
&& echo "Build Complete for ./taylor.o"
