#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1  // %5d etc
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS   1  // %.2f etc
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS       0  // %f/%F - off, no FPU in this kernel
#define NANOPRINTF_USE_FLOAT_HEX_FORMAT_SPECIFIER    0  // %a/%A - requires FLOAT=1, so must be 0
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS       1  // %hhd etc
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS       1  // %lld/%llx etc
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS      0  // %b - unused
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS   0  // %n - unused
#define NANOPRINTF_USE_ALT_FORM_FLAG                 0  // # modifier - unused

#define NANOPRINTF_IMPLEMENTATION
#include "nanoprintf.h"
