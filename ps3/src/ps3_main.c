#include "ps3_platform.h"
#include "ps3_disc.h"
#include "ps3_settings.h"

#include <stdio.h>

#if defined(TARGET_PS3)
#include <sys/process.h>
SYS_PROCESS_PARAM(1001, 0x100000)
#endif

int main(int argc, char** argv) {
#if defined(ACGC_PS3_BOOTSTRAP_ONLY)
    (void)argc; (void)argv;
    ps3_platform_init();

    printf("Animal Crossing GC PS3 bootstrap\n");
    printf("This package validates the PS3 build/launch pipeline.\n");
    printf("Full game sources are staged under ps3/src for the next porting pass.\n");

    ps3_platform_wait_for_exit_request();
    ps3_platform_shutdown();
    return 0;
#else
    extern void ac_entry(void);
    extern int boot_main(int argc, const char** argv);

    ps3_settings_load();
    ps3_platform_init();
    ps3_disc_init();

    ac_entry();
    boot_main(argc, (const char**)argv);

    ps3_disc_shutdown();
    ps3_platform_shutdown();
    return 0;
#endif
}
