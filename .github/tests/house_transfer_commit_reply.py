"""Drop MariaDB's successful COMMIT reply, not the actual transaction.

Opt-in, disposable full-schema DB only. The loopback proxy forwards to a local
Unix socket and never uses or changes the server's private config/database.
"""
import os
import select
import socket
import subprocess
import sys
import threading


def receive(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise EOFError
        data.extend(chunk)
    return bytes(data)


def packet(sock):
    header = receive(sock, 4)
    return header + receive(sock, int.from_bytes(header[:3], "little"))


class CommitReplyProxy:
    def __init__(self, refuse_reconnect, drop_commit=2):
        self.refuse_reconnect = refuse_reconnect
        self.dropped = threading.Event()
        self.stopped = threading.Event()
        self.lock = threading.Lock()
        self.commits = 0
        self.drop_commit = drop_commit
        self.errors = []
        self.connections = []
        self.workers = []
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.listener.settimeout(0.2)
        self.port = self.listener.getsockname()[1]
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def accept(self):
        while not self.stopped.is_set():
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            if self.refuse_reconnect and self.dropped.is_set():
                client.close()
                continue
            worker = threading.Thread(target=self.forward, args=(client,), daemon=True)
            self.workers.append(worker)
            worker.start()

    def forward(self, client):
        server = socket.socket(socket.AF_UNIX)
        with client, server:
            self.connections.extend((client, server))
            try:
                server.connect(os.environ.get("TFS_SAVE_TEST_SOCKET", "/run/mysqld/mysqld.sock"))
                drop_reply = False
                while not self.stopped.is_set():
                    readable, _, _ = select.select((client, server), (), (), 0.2)
                    for source in readable:
                        data = packet(source)
                        if source is client:
                            if data[4:5] == b"\x03" and data[5:].strip().upper() == b"COMMIT":
                                with self.lock:
                                    self.commits += 1
                                    # Seed commits precede the production transfer.
                                    # The trade also seeds both player inventories.
                                    drop_reply = self.commits == self.drop_commit
                            server.sendall(data)
                        else:
                            if drop_reply:
                                if data[4:5] != b"\x00":
                                    raise RuntimeError("Transfer COMMIT did not succeed before reply was dropped")
                                self.dropped.set()
                                return
                            client.sendall(data)
            except (EOFError, ConnectionError):
                pass
            except OSError as error:
                if not self.stopped.is_set():
                    self.errors.append(str(error))
            except Exception as error:
                self.errors.append(str(error))

    def close(self):
        self.stopped.set()
        self.listener.close()
        self.thread.join(timeout=2)
        for connection in self.connections:
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        for worker in self.workers:
            worker.join(timeout=2)


def main():
    database = os.environ.get("TFS_HOUSE_TEST_DB", "")
    if not database.startswith("tfs_save_audit_house_"):
        raise SystemExit("An explicitly named disposable house-test database is required")
    if not hasattr(socket, "AF_UNIX"):
        raise SystemExit("This proxy test requires local MariaDB's Unix socket")
    executable = os.path.abspath(sys.argv[1])
    for refuse, trade in ((False, False), (True, False), (False, True)):
        proxy = CommitReplyProxy(refuse, 3 if trade else 2)
        try:
            environment = dict(os.environ, TFS_TEST_PROXY_PORT=str(proxy.port))
            mode = "--trade-commit-reply-loss" if trade else "--commit-unresolved" if refuse else "--commit-reply-loss"
            subprocess.run([executable, mode], env=environment, check=True, timeout=120)
            if not proxy.dropped.is_set() or proxy.errors:
                raise RuntimeError(f"COMMIT reply fault was not exercised cleanly: {proxy.errors}")
        finally:
            proxy.close()
        # A fresh process connects directly and confirms the committed transfer
        # retained every identity/stack exactly once, even in fail-closed mode.
        subprocess.run([executable, "--trade-verify-after" if trade else "--verify-after"], check=True, timeout=60)
    print("3 lost-COMMIT-reply scenarios passed: transfer/trade reconciliation and fail-closed restart")


if __name__ == "__main__":
    main()
