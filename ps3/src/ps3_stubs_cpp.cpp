/* ps3_stubs_cpp.cpp - JSystem vtable stubs the decomp headers declare but never define */
#include "ps3_platform.h"

/* JSUOutputStream ??? method bodies missing from decomp, needed for vtable */
#include "JSystem/JSupport/JSUInputStream.h"
#include "JSystem/JSupport/JSURandomInputStream.h"

JSUOutputStream::~JSUOutputStream() {}

int JSUOutputStream::skip(s32 amount) {
    (void)amount;
    return 0;
}

int JSURandomOutputStream::getAvailable() const {
    return 0;
}

int JSURandomOutputStream::skip(s32 amount) {
    int s = this->seekPos(amount, static_cast<JSUStreamSeekFrom>(SEEK_CUR));
    return s;
}

/* JKRHeap::destroy ??? declared but never compiled in decomp */
#include "JSystem/JKernel/JKRHeap.h"

void JKRHeap::destroy() {}


#include "JSystem/JKernel/JKRThread.h"

JKRTask::Request* JKRTask::searchBlank() {
    return nullptr;
}

#include "JSystem/JUtility/JUTFont.h"

#if !defined(TARGET_PS3)
const ResFONT JUTResFONT_Ascfont_fix12 = {};
#endif
