#!/usr/bin/env python3
"""Drive the Zephyr shell on the console socket of a Renode machine.

The Renode scripts in renode/ put the console UART on a socket terminal (port 3456). This connects to
it, prints what the firmware writes for a while, then sends each command and prints its reply, so a
headless run (CI, a container without a display) can read 'kernel heap', 'kernel thread stacks',
'mem heaps' and the rest. Interactively, telnet does the same.

    scripts/renode_console.py --settle 25 "kernel heap" "kernel thread stacks" "mem heaps"
"""

import argparse
import socket
import sys
import time


def receive(sock, seconds):
    end = time.monotonic() + seconds
    chunks = []
    sock.settimeout(0.2)
    while time.monotonic() < end:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            continue
        if not data:
            break
        chunks.append(data)
    return b"".join(chunks).decode("utf-8", "replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("commands", nargs="*", help="shell commands to send, in order")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=3456, help="console socket of the Renode script (default 3456)")
    parser.add_argument("--settle", type=float, default=5.0, help="seconds of console output to collect first")
    parser.add_argument("--reply", type=float, default=3.0, help="seconds to wait for each command's reply")
    args = parser.parse_args()

    try:
        sock = socket.create_connection((args.host, args.port), timeout=5)
    except OSError as error:
        sys.exit(f"cannot connect to {args.host}:{args.port}: {error} (is the Renode script running?)")

    sys.stdout.write(receive(sock, args.settle))
    for command in args.commands:
        sock.sendall(b"\r\n")
        receive(sock, 0.5)
        sock.sendall(command.encode() + b"\r\n")
        sys.stdout.write(f"\n### {command}\n")
        sys.stdout.write(receive(sock, args.reply))
    sock.close()


if __name__ == "__main__":
    main()
