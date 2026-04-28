# Network IO

Modules: `std/net/ipaddr`, `std/net/stream/bufferstream`, `std/net/stream/connection`, `std/net/transports/udp_syscall`, `std/net/transports/tcp_syscall`

Key APIs: `parseIpAddr/ipAddrToString`, `newBufferStream/write/read`, `udpSocketOpenDgram/udpBindAddr/udpSendTo/udpRecvFromNonblock`, `tcpListen/tcpAccept/tcpConnect/tcpSend/tcpRecv`

Example: `examples/std/net_io.cheng`

Notes: The baseline is limited to loopback-only socket tests. UDP remains the mature transport path; TCP is introduced here as a minimal client/server syscall transport for localhost scenarios.
