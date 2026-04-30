/* Minimal PSL1GHT bootstrap used by ps3/Makefile.
 * The full game port lives in ps3/src and is wired separately. */
#include <stdio.h>

#if defined(TARGET_PS3)
#include <sys/process.h>
SYS_PROCESS_PARAM(1001, 0x100000)
#endif

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("Animal Crossing GC PS3 bootstrap\n");
    printf("Full game port sources are in USRDIR/ps3/src during development.\n");
    return 0;
}
