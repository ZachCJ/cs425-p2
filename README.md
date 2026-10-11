# Project 2

- Name: Zach Johnston
- Email: <zacharyjohnston324@u.boisestate.edu>
- Class: CS425-001

## Known Bugs or Issues

Not really a bug but make sure cs425_relay.py is running before using make leak otherwise it won't work.

## Experience

This project was long even with using copilot, and I ended up using 40% of my tokens doing it. The way I used copilot was first framing
out how I wanted aspects of the project structure and defining bounds, and protocols. Where most of my token usage came from was the implementation, and the back and forth that needed to occur with testing as copilot struggled to test the implementation.

This brings me to the dumbest thing I found out was that VScode can overwrite the PATH variable of the system when copilot launches so you
will lose the PATH with all your installed packages if you boot VScode before the copilot CLI. THE FACT THAT THIS HAPPENS IS INFURIATING WHAT THE HECK MICROSOFT GET IT TOGETHER. The path it overwrites it with doesn't even give the vscode extensions. I lost so many hours to trying to fix issues with the PATH not getting the packages and programs I needed for copilot.

### Task 4 Explanation i.e Design

The three layer split mainly exists for testability (I know it's in the title). If we put all the segments like packets, Go-Back-N state machines, and the I/O all in one giant method we'd have no way of testing it in a way where we can tell that multiple segments are failing at the same time. This allows us to save time on re-running files when debugging because each hidded bug adds another back and forth to the process of debugging.

### Table of Run Results Task 6

| Window | Loss | Corrupt | Dup | Time    | Throughput  |
| ------ | ---- | ------- | --- | ------- | ----------- |
| 1      | 0    | 0       | 0   | 103.7 s | 9.878 KiB/s |
| 16     | 0    | 0       | 0   | 6.6 s   | 155.2 KiB/s |
| 1      | 0.05 | 0       | 0   | 125.9 s | 8.129 KiB/s |
| 16     | 0.05 | 0       | 0   | 18.08 s | 56.63 KiB/s |

Based on my results the round trip was 101.235ms which doesn't have much of a gap and it most likely comes from processing time. This
seems very fast however my machine is also pretty fast normally so this might not be abnormal. The speedup gets close at 15.71 times but likely doesn't reach it do aditional processing time.

The 16 window slows down so much compared to the 1 window because it essentially has more to lose when a loss occurs. When a send fails with a window of 1 only 1 packet needs to be resent, where a window of 16 there is the potential for 16 entire packet needing to be resent.

Here is an example of how to include a plot in your README:

## UDP Packet Header

Every UDP packet uses an explicit 10-byte header followed by an optional
payload. Multi-byte fields are encoded in network byte order. The reserved
byte must always be zero.

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 byte | Type | `0` = DATA, `1` = ACK, `2` = FIN |
| 1 | 1 byte | Reserved | Always `0` |
| 2 | 4 bytes | Sequence | Packet sequence number or cumulative ACK number |
| 6 | 2 bytes | Payload length | Payload size in bytes; maximum 1024 |
| 8 | 2 bytes | Checksum | RFC 1071 Internet checksum of the payload |
| 10 | Variable | Payload | Zero to 1024 bytes |

The receiver rejects packets with an unsupported type, a nonzero reserved
byte, an invalid payload length, or a checksum mismatch. DATA packets must
also arrive in sequence. FIN has a zero-length payload and terminates the
receive loop after the final expected sequence number is received.
