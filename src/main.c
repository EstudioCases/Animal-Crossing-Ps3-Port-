#include "main.h"

#include "boot.h"
#include "irqmgr.h"
#include "sys_stacks.h"
#include "graph.h"
#include "libultra/osMesg.h"
#include "libultra/os_thread.h"
#include "jsyswrap.h"
#include "m_card.h"
#include "_mem.h"
#include "padmgr.h"
#include "libultra/setthreadpri.h"
#include "m_msg.h"
#include "Famicom/famicom.h"
#include "m_debug.h"
#include "dolphin/os.h"
#include "libforest/osreport.h"
#include "m_land.h"

// TODO: actually add all the stacks and headers

OSThread graphThread;
static OSMessage serialMsgBuf;
static OSMessageQueue l_serialMsgQ;
u8 SegmentBaseAddress[0x40];

int ScreenWidth = SCREEN_WIDTH;
int ScreenHeight = SCREEN_HEIGHT;

extern void mainproc(void* val) {

    irqmgr_client_t irqClient;
    OSMessageQueue irqMgrMsgQueue;
    OSMessage irqMsgBuf[10];
    OSMessage msg;

    ScreenWidth = SCREEN_WIDTH;
    ScreenHeight = SCREEN_HEIGHT;

#ifdef TARGET_PC
    OSReport("[PC] mainproc: JW_BeginFrame/EndFrame...\n");
#elif defined(TARGET_PS3)
    OSReport("[PS3] mainproc: first frame pump...\n");
#endif
    JW_BeginFrame();
#ifdef TARGET_PS3
    OSReport("[PS3] mainproc: first JW_BeginFrame done\n");
#endif
    JW_EndFrame();
#ifdef TARGET_PS3
    OSReport("[PS3] mainproc: first JW_EndFrame done\n");
    OSReport("[PS3] mainproc: mCD_init_card...\n");
#endif
    mCD_init_card();
#ifdef TARGET_PS3
    OSReport("[PS3] mainproc: mCD_init_card done\n");
#endif

    osCreateMesgQueue(&l_serialMsgQ, &serialMsgBuf, 1);
    osCreateMesgQueue(&irqMgrMsgQueue, irqMsgBuf, 10);
#ifdef TARGET_PC
    OSReport("[PC] mainproc: CreateIRQManager...\n");
#endif
    CreateIRQManager(irqmgrStack + IRQMGR_STACK_SIZE, IRQMGR_STACK_SIZE, 18, 1);
    irqmgr_AddClient(&irqClient, &irqMgrMsgQueue);
    memset(padmgrStack, 0xEB, PADMGR_STACK_SIZE);

#ifdef TARGET_PC
    OSReport("[PC] mainproc: padmgr_Create...\n");
#endif
    padmgr_Create(&l_serialMsgQ, 7, 15, padmgrStack + PADMGR_STACK_SIZE, PADMGR_STACK_SIZE);

    osCreateThread2(&graphThread, 4, graph_proc, val, graphStack + GRAPH_STACK_SIZE, GRAPH_STACK_SIZE, 8);

#ifdef TARGET_PS3
    OSReport("[PS3] mainproc: second frame pump...\n");
#endif
    JW_BeginFrame();
#ifdef TARGET_PS3
    OSReport("[PS3] mainproc: second JW_BeginFrame done\n");
#endif
    JW_EndFrame();
#ifdef TARGET_PS3
    OSReport("[PS3] mainproc: second JW_EndFrame done\n");
#endif

#if !defined(TARGET_PC) && !defined(TARGET_PS3)
    osStartThread(&graphThread);
#endif
    osSetThreadPri(NULL, 5);

#ifdef TARGET_PC
    OSReport("[PC] mainproc: JW_Init3...\n");
#elif defined(TARGET_PS3)
    OSReport("[PS3] mainproc: JW_Init3...\n");
#endif
    JW_Init3();
#ifdef TARGET_PC
    OSReport("[PC] mainproc: mMsg_aram_init2...\n");
#elif defined(TARGET_PS3)
    OSReport("[PS3] mainproc: mMsg_aram_init2...\n");
#endif
    mMsg_aram_init2();
#ifdef TARGET_PC
    OSReport("[PC] mainproc: mLd_StartFlagOn...\n");
#elif defined(TARGET_PS3)
    OSReport("[PS3] mainproc: mLd_StartFlagOn...\n");
#endif
    mLd_StartFlagOn();
#ifdef TARGET_PC
    OSReport("[PC] mainproc: famicom_mount_archive...\n");
#elif defined(TARGET_PS3)
    OSReport("[PS3] mainproc: famicom_mount_archive...\n");
#endif
    famicom_mount_archive();

#ifdef TARGET_PC
    OSReport("[PC] mainproc: JC_JKRAramHeap_dump...\n");
#endif
    JC_JKRAramHeap_dump(JC_JKRAram_getAramHeap());
    osSetThreadPri(NULL, 13);

#if defined(TARGET_PC) || defined(TARGET_PS3)
    /* These ports drive the graph loop inline to avoid racing the boot thread. */
#ifdef TARGET_PC
    OSReport("[PC] mainproc: calling graph_proc directly (single-threaded)...\n");
#elif defined(TARGET_PS3)
    OSReport("[PS3] mainproc: calling graph_proc directly (single-threaded)...\n");
#endif
    graph_proc(val);
#ifdef TARGET_PC
    OSReport("[PC] mainproc: graph_proc returned\n");
    {
        extern void pc_platform_shutdown(void);
        pc_platform_shutdown();
        exit(0);
    }
#endif
#else
    do {
        msg = NULL;
        while (irqMgrMsgQueue.usedCount != 0) {
            osRecvMesg(&irqMgrMsgQueue, NULL, 0);
        }

        osRecvMesg(&irqMgrMsgQueue, &msg, 1);
    } while (msg != NULL);
#endif
}

u32 entry(void) {
#ifdef TARGET_PC
    OSReport("[PC] entry() called\n");
#endif
    padmgr_Init(NULL);
    new_Debug_mode();

    SETREG(SREG, 0, 0);
#ifdef TARGET_PC
    OSReport("[PC] entry: calling mainproc...\n");
#endif
    mainproc(NULL);

    return 0;
}

void main(void) {
    OSReport("どうぶつの森 main2 開始\n");
    HotStartEntry = &entry;
}
