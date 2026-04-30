set -e

cd /d/0apelotas/descargas2/ACGC-PS3-Port/ps3/build-full
/usr/bin/ccmake.exe -S$(CMAKE_SOURCE_DIR) -B$(CMAKE_BINARY_DIR)
