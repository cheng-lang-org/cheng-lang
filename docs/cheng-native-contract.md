# Cheng Native Contract

This document preserves the legacy native-contract markers still consumed by
`verify_backend_native_contract` in older tooling binaries. The active closure
source no longer treats this document as the primary authority, but the markers
remain normative for compatibility with the current canonical verification path.

Normative markers:

- `native_contract.version=1`
- `native_contract.scheme.id=CNC`
- `native_contract.scheme.name=cheng_native_contract`
- `native_contract.scheme.normative=1`
- `native_contract.enforce.mode=hard_fail`

Pillars:

- `native_contract.pillar.no_float=1`
- `native_contract.pillar.no_syscall=1`
- `native_contract.pillar.native_charge_symbol=1`
- `native_contract.pillar.native_gas_counter=1`

Required gate:

- `native_contract.required_gate.backend.native_contract=1`
