set -e

cd /d/0apelotas/descargas2/ACGC-PS3-Port/ps3/build-full
/usr/bin/cmake.exe -E make_directory /d/0apelotas/descargas2/ACGC-PS3-Port/ps3/build-full/pkg/USRDIR
/usr/bin/cmake.exe -E copy_directory /d/0apelotas/descargas2/ACGC-PS3-Port/ps3/data /d/0apelotas/descargas2/ACGC-PS3-Port/ps3/build-full/pkg/USRDIR
