#include "system_helpers.h"

/*
 * Mobile export shim:
 * the generated compatibility app payload does not consume the legacy
 * `system_helpers.c` runtime anymore. Keep a tiny translation unit so old
 * project templates still compile while the export pipeline stays detached
 * from `src/runtime/native/system_helpers.c`.
 */
