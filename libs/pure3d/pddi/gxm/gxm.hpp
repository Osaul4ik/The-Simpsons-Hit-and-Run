//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#ifndef _GXM_HPP_
#define _GXM_HPP_

// stub GXM header, all pddi gxm code uses this instead of '#include <psp2/gxm.h>

#include <psp2/gxm.h>
#include <psp2/display.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/sysmem.h>
#include <pddi/base/debug.hpp>

#ifdef PDDI_USE_ASSERTS
bool pddiGxmAssert(const char* file, int line, unsigned int err, const char* cond = nullptr);
#define CHK_GXM(c) pddiGxmAssert(__FILE__, __LINE__, (c), #c)
#elif defined(RAD_LOG)
// Release build with SRR2_LOG: report every failing GXM call (file:line, expression, error
// code) to the log instead of failing silently.
bool pddiGxmLog(const char* file, int line, unsigned int err, const char* cond);
#define CHK_GXM(c) pddiGxmLog(__FILE__, __LINE__, (unsigned int)(c), #c)
#else
#define CHK_GXM(c) ((c) == SCE_OK)
#endif

#endif