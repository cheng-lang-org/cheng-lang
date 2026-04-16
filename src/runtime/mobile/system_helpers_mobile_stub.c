#include "system_helpers.h"

/*
 * Mobile export shim:
 * exported mobile templates now take their live compatibility paths from
 * `src/tooling/backend_runtime_abi_contract.env`. Keep a tiny translation
 * unit so old project templates still compile while export stays detached
 * from the retired monolithic `src/runtime/native/system_helpers.c`.
 */
