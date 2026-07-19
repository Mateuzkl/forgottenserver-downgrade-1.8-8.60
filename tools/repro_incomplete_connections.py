#!/usr/bin/env python3
"""Reproduce the stale per-IP slot bug by closing before sending a packet."""

import argparse
import socket
import time


def open_incomplete(host: str, port: int, timeout: float) -> None:
    connection = socket.create_connection((host, port), timeout=timeout)
    connection.close()


def probe(host: str, port: int, timeout: float) -> bool:
    connection = socket.create_connection((host, port), timeout=timeout)
    connection.settimeout(timeout)
    try:
        data = connection.recv(1)
        return data != b""
    except ConnectionResetError:
        return False
    except TimeoutError:
        # An admitted incomplete connection waits for the two-byte packet header.
        return True
    finally:
        connection.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("--port", type=int, default=7171)
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--delay-ms", type=int, default=750)
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()

    for attempt in range(1, args.count + 1):
        open_incomplete(args.host, args.port, args.timeout)
        print(f"incomplete connection {attempt}/{args.count}")
        time.sleep(args.delay_ms / 1000.0)

    time.sleep(0.5)
    admitted = probe(args.host, args.port, args.timeout)
    print("probe admitted" if admitted else "probe immediately closed")
    return 0 if admitted else 1


if __name__ == "__main__":
    raise SystemExit(main())
