set -e

cd /d/0apelotas/descargas2/ACGC-PS3-Port/ps3/build-full
/usr/bin/cmake.exe -E make_directory /d/0apelotas/descargas2/ACGC-PS3-Port/ps3/build-full/pkg/USRDIR
/usr/local/ps3dev/bin/fself.exe /d/0apelotas/descargas2/ACGC-PS3-Port/ps3/build-full/ACGCPS3 /d/0apelotas/descargas2/ACGC-PS3-Port/ps3/build-full/pkg/USRDIR/EBOOT.BIN
