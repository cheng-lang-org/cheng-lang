# cheng-quic

Cheng QUIC transport stack extracted from `cheng-libp2p`.

## Import Prefix
- Use `cheng/quic/...`.

## Current Dependency Model
- `cheng-quic` does **not** depend on `pkg://cheng/libp2p`.
- Shared building blocks live in:
  - `std/*` (foundation + crypto + tls/x509)
  - `std/net/*`
  - `std/multiformats/*`
  - `pkg://cheng/observability` (`cheng/observability/...`)
- `scripts/verify.sh` includes a static gate that rejects `import cheng/libp2p/...` under `src/`.

## Verify
- Run `scripts/verify.sh --mode:msquic` for msquic-focused smoke coverage.
- Run `scripts/verify.sh --mode:quic` for quic transport smoke coverage.
