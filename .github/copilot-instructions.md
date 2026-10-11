# Copilot instructions for `cs425-p2`

## Build, test, and analysis commands

Run commands from the repository root in a Bash-capable environment (Linux,
macOS, or the provided C++ dev container). The Makefile uses Bash syntax and
GNU Make; the Windows host itself is not the supported build shell.

```bash
make all          # debug, release, coverage-test, and ASan-test builds
make release      # build/release/myapp
make debug        # build/debug/myapp_d (ASan)
make test         # build/tests/myapp_t (coverage instrumentation)
make debug-test   # build/debug-test/myapp_td (ASan + tests)
make check        # run the Unity test executable
make leak-test    # run the ASan test executable with leak detection
make report       # run tests and generate gcovr text/HTML coverage
make clean        # remove build artifacts and submission-report.md
make help         # list targets
```

The Unity runner is `tests/lab-test.c`. There is no command-line test filter:
to run one test in isolation, temporarily leave only that test's `RUN_TEST(...)`
entry in the runner, then run `make test && ./build/tests/myapp_t`. The normal
suite is `make test && make check`; CI additionally runs `make all` and
`make leak-test`. `make report` requires `gcovr`.

There is no separate lint target. Compilation is the static check and uses
strict warnings and hardening flags from `Makefile`, including
`-Wall -Wextra -Wconversion`, `-Wsign-conversion`, `-Wimplicit-fallthrough`,
stack protection, and selected warnings promoted to errors.

To exercise the network path, start the standard-library-only Python 3.9+
relay before the C programs:

```bash
python3 src/cs425_relay.py --delay 50 --seed 1
```

The relay pairs `HELLO <session> recv` with
`HELLO <session> send <loss> <corrupt> <dup>`. Application commands are:

```text
./build/release/myapp send -s <session> [-w window] [-T timeout-ms] [-l loss] [-c corrupt] [-d dup] [-p port] <relay> <input-file>
./build/release/myapp recv -s <session> [-p port] <relay> <output-file>
```

## Architecture

This is a UDP file-transfer application with a separate relay. The `send` mode
reads a file, splits it into 1024-byte payloads, sends them through the relay
using a Go-Back-N window, and retransmits from the earliest unacknowledged
packet after the base packet times out. The `recv` mode accepts only
checksum-valid, strictly in-order data, writes accepted payloads, and sends
cumulative ACKs; it does not own retransmission timers.

`src/lab.c`/`src/lab.h` contain the testable logic: CLI parsing, owned
configuration strings, packetization, RFC 1071 Internet checksums, sender state,
and receiver state. `src/main.c` owns DNS/socket operations, relay
registration, wire encoding/decoding, ACK and FIN handling, and
the send/receive loops. Compiling with `-DTEST` renames its network `main` so
the production logic can link into the Unity test executable. The relay in
`src/cs425_relay.py` is independent of the C executable and uses only Python's
standard library.

## Repository-specific conventions

- Keep network- and OS-dependent code in `src/main.c`; put deterministic,
  unit-testable behavior in `src/lab.c`/`src/lab.h`.
- `lab_parse_args` allocates `session`, `relay`, and `file`; release them with
  `lab_config_free`, including on every error path. `lab_read_packets`
  allocates packet buffers; release them with `lab_packet_list_free`.
- `LabSenderState.base` is the earliest unacknowledged packet and `next` is the
  next packet to send. ACKs are cumulative through already acknowledged
  packets. A timeout calls `lab_sender_reset_to_base`, which sets `next = base`
  and clears retransmission timer state for unacknowledged packets.
- Receiver acceptance is strict in-order and checksum-validated. Corrupt,
  duplicate, out-of-order, malformed, or otherwise rejected data must not be
  written; the receiver still sends its current ACK state.
- The wire format is a 10-byte header followed by at most
  `LAB_MAX_PAYLOAD` bytes: 1-byte type, 1-byte reserved field (always zero),
  4-byte sequence, 2-byte payload length, and 2-byte checksum, all at explicit
  offsets. Packet validity comes
  from the supported type range, exact header-plus-payload length, payload
  limit, and checksum; there is no magic field. Keep
  `encode_packet`/`decode_packet` changes in sync. `WIRE_DATA` is type 0,
  `WIRE_ACK` is type 1, and `WIRE_FIN` is type 2. Malformed, unsupported,
  checksum-invalid, duplicate, or out-of-order packets are discarded without
  an ACK; only valid in-order data and a valid FIN receive responses.
- CLI validation follows the existing limits and error-buffer pattern:
  sessions are 1–32 lowercase letters, digits, or hyphens; the default window
  is 8, timeout is 250 ms, relay port is 4250, and loss/corruption/duplication
  rates are capped at 0.5.
- Add Unity tests to `tests/lab-test.c` and register them with `RUN_TEST`.
  The test build links both production C files and the vendored harness under
  `tests/harness`.
- Generated objects, executables, coverage output, and submission reports
  belong in `build/` or are produced by the scripts/workflows. Do not hand-edit
  generated artifacts.
