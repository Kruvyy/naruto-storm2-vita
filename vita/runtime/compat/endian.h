// <endian.h> for the VitaSDK (newlib keeps it in <machine/endian.h>), with
// the glibc-style macros that thirdparty/crypto/sha256.cpp tests.
#pragma once

#include <machine/endian.h>

#ifndef __BYTE_ORDER
#define __BYTE_ORDER __BYTE_ORDER__
#endif
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN __ORDER_BIG_ENDIAN__
#endif
