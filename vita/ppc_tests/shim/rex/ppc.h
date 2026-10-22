// PS Vita test harness shim for <rex/ppc.h>.
// Generated guest code only needs the register context, intrinsics and the
// PPCFunc type. The real header also pulls in hook/function/stack helpers
// that depend on the desktop runtime (kernel state, flat guest memory).
#pragma once

#include <rex/types.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/ppc/intrinsics.h>
