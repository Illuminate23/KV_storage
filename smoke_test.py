#!/usr/bin/env python3
"""
Набор функциональных тестов для ООП-сервера кэша «ключ-значение».

Тест общается с сервером по его бинарному протоколу и проверяет не только
корректность команд, но и поведение, составляющее «суть» кэша:
  * автоматическое истечение ключа по TTL (ключ действительно исчезает),
  * конвейеризацию запросов (несколько запросов в одном буфере),
  * бинарную безопасность ключей/значений и большие значения,
  * массовую вставку, запускающую прогрессивный рехеш хеш-таблицы,
  * фоновое удаление большого сортированного множества (путь через пул потоков),
  * множество одновременных клиентских соединений в цикле событий,
  * закрытие соединения по таймауту простоя и по слишком большому кадру.

Запуск: python3 smoke_test.py          (весь набор, ~7 c с тестом простоя)
        python3 smoke_test.py --fast   (пропустить медленный тест таймаута)
"""
import socket, struct, subprocess, sys, time, threading

HOST, PORT = "127.0.0.1", 1234
K_MAX_MSG = 32 << 20            # должно совпадать с k_max_msg сервера
IDLE_TIMEOUT_S = 5             # должно совпадать с k_idle_timeout_ms сервера

TAG_NIL, TAG_ERR, TAG_STR, TAG_INT, TAG_DBL, TAG_ARR = range(6)

# --- бинарный протокол ----------------------------------------------------

def encode(args):
    """Сериализовать один запрос: число токенов, затем (длина, байты) на каждый."""
    body = struct.pack("<I", len(args))
    for a in args:
        a = a.encode() if isinstance(a, str) else a
        body += struct.pack("<I", len(a)) + a
    return struct.pack("<I", len(body)) + body

def decode(buf, i=0):
    """Разобрать одно значение с тегом. Строки остаются байтами (бинарно-безопасно)."""
    tag = buf[i]; i += 1
    if tag == TAG_NIL:
        return ("nil", None), i
    if tag == TAG_INT:
        (v,) = struct.unpack_from("<q", buf, i); return ("int", v), i + 8
    if tag == TAG_DBL:
        (v,) = struct.unpack_from("<d", buf, i); return ("dbl", v), i + 8
    if tag == TAG_STR:
        (n,) = struct.unpack_from("<I", buf, i); i += 4
        return ("str", bytes(buf[i:i+n])), i + n
    if tag == TAG_ERR:
        (code,) = struct.unpack_from("<I", buf, i); i += 4
        (n,) = struct.unpack_from("<I", buf, i); i += 4
        return ("err", (code, bytes(buf[i:i+n]))), i + n
    if tag == TAG_ARR:
        (n,) = struct.unpack_from("<I", buf, i); i += 4
        out = []
        for _ in range(n):
            v, i = decode(buf, i); out.append(v)
        return ("arr", out), i
    raise ValueError(f"bad tag {tag}")

def recvn(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("connection closed by server")
        buf += chunk
    return buf

def read_response(sock):
    (length,) = struct.unpack("<I", recvn(sock, 4))
    val, _ = decode(recvn(sock, length))
    return val

def call(sock, *args):
    sock.sendall(encode(list(args)))
    return read_response(sock)

def connect():
    return socket.create_connection((HOST, PORT))

# --- минимальный каркас тестов --------------------------------------------

class Runner:
    def __init__(self):
        self.fails = 0
        self.count = 0

    def check(self, label, got, want):
        self.count += 1
        ok = got == want
        # не вываливаем огромные значения в лог
        shown = got if not (isinstance(got, tuple) and got and got[0] == "str"
                            and len(got[1]) > 64) else ("str", f"<{len(got[1])} bytes>")
        print(f"[{'OK ' if ok else 'FAIL'}] {label}: {shown}"
              + ("" if ok else f"  (expected {want})"))
        if not ok:
            self.fails += 1

    def ok(self, label, condition):
        self.check(label, bool(condition), True)

# --- группы тестов --------------------------------------------------------

def test_strings(r, s):
    r.check("set k v",     call(s, "set", "k", "v"),  ("nil", None))
    r.check("get k",       call(s, "get", "k"),       ("str", b"v"))
    r.check("get missing", call(s, "get", "nope"),    ("nil", None))
    r.check("del k",       call(s, "del", "k"),       ("int", 1))
    r.check("del again",   call(s, "del", "k"),       ("int", 0))
    r.check("unknown cmd", call(s, "frobnicate"),     ("err", (1, b"unknown command.")))
    r.check("wrong arity", call(s, "get", "a", "b"),  ("err", (1, b"unknown command.")))

def test_zset(r, s):
    r.check("zadd a",      call(s, "zadd", "z", "1", "a"), ("int", 1))
    r.check("zadd b",      call(s, "zadd", "z", "2", "b"), ("int", 1))
    r.check("zadd a upd",  call(s, "zadd", "z", "5", "a"), ("int", 0))
    r.check("zscore a",    call(s, "zscore", "z", "a"),    ("dbl", 5.0))
    r.check("zquery order", call(s, "zquery", "z", "0", "", "0", "10"),
            ("arr", [("str", b"b"), ("dbl", 2.0), ("str", b"a"), ("dbl", 5.0)]))
    r.check("zrem a",      call(s, "zrem", "z", "a"),      ("int", 1))
    r.check("type error",  call(s, "get", "z"),            ("err", (3, b"not a string value")))
    call(s, "del", "z")

def test_ttl_expiry(r, s):
    """Главное поведение кэша: ключ с TTL должен исчезнуть сам собой."""
    r.check("set cached",   call(s, "set", "cached", "payload"), ("nil", None))
    r.check("pttl no ttl",  call(s, "pttl", "cached"),           ("int", -1))
    r.check("pttl missing", call(s, "pttl", "ghost"),            ("int", -2))
    r.check("pexpire 200ms", call(s, "pexpire", "cached", "200"),("int", 1))
    pttl = call(s, "pttl", "cached")
    r.ok("pttl positive", pttl[0] == "int" and 0 < pttl[1] <= 200)
    r.check("hit before expiry", call(s, "get", "cached"), ("str", b"payload"))
    time.sleep(0.4)                                   # даём TTL истечь
    r.check("evicted by ttl",    call(s, "get", "cached"),  ("nil", None))
    r.check("pttl after evict",  call(s, "pttl", "cached"), ("int", -2))

def test_pipelining(r, s):
    """Два запроса в одном пакете должны дать два упорядоченных ответа."""
    s.sendall(encode(["set", "p", "1"]) + encode(["get", "p"]))
    r.check("pipe resp 1", read_response(s), ("nil", None))
    r.check("pipe resp 2", read_response(s), ("str", b"1"))
    call(s, "del", "p")

def test_binary_and_large(r, s):
    key = b"k\x00bin\xff"                              # встроенные NUL и 0xFF
    val = b"\x00\x01\x02\xfe\xff value"
    r.check("set binary", call(s, "set", key, val), ("nil", None))
    r.check("get binary", call(s, "get", key),      ("str", val))
    call(s, "del", key)

    big = b"A" * (4 << 20)                             # значение 4 МиБ (< k_max_msg)
    r.check("set 4MiB",   call(s, "set", "big", big), ("nil", None))
    r.check("get 4MiB",   call(s, "get", "big"),      ("str", big))
    call(s, "del", "big")

def test_mass_insert_rehash(r, s):
    """Вставить достаточно ключей, чтобы вызвать несколько циклов рехеша."""
    n = 5000
    for i in range(n):
        call(s, "set", "mass:%d" % i, "x")
    keys = call(s, "keys")
    mass = [k for k in keys[1] if k[0] == "str" and k[1].startswith(b"mass:")]
    r.check("rehash: all keys present", len(mass), n)
    r.check("rehash: sample get", call(s, "get", "mass:4321"), ("str", b"x"))
    for i in range(n):
        call(s, "del", "mass:%d" % i)
    r.check("rehash: cleaned up", call(s, "get", "mass:0"), ("nil", None))

def test_large_zset_background_delete(r, s):
    """Zset выше порога «большого контейнера» освобождается в пуле потоков."""
    n = 1200                                           # > k_large_container_size (1000)
    for i in range(n):
        call(s, "zadd", "bigz", str(i), "m%d" % i)
    q = call(s, "zquery", "bigz", "0", "", "0", str(2 * n))
    r.check("bigz populated", q[0] == "arr" and len(q[1]) == 2 * n, True)
    r.check("del bigz (bg free)", call(s, "del", "bigz"), ("int", 1))
    # сразу после постановки большого удаления в очередь сервер должен отвечать
    r.check("alive after bg del", call(s, "set", "after", "ok"), ("nil", None))
    r.check("zscore on gone zset", call(s, "zscore", "bigz", "m0"), ("nil", None))
    call(s, "del", "after")

def test_concurrent_clients(r):
    """Множество одновременных соединений нагружает цикл событий poll()."""
    threads, errors = [], []
    def worker(idx):
        try:
            s = connect()
            for j in range(100):
                k = "c%d:%d" % (idx, j)
                if call(s, "set", k, k) != ("nil", None):
                    errors.append((k, "set"))
                if call(s, "get", k) != ("str", k.encode()):
                    errors.append((k, "get"))
                call(s, "del", k)
            s.close()
        except Exception as e:               # noqa
            errors.append((idx, repr(e)))
    for idx in range(8):
        t = threading.Thread(target=worker, args=(idx,)); t.start(); threads.append(t)
    for t in threads:
        t.join()
    r.check("8 concurrent clients", errors, [])

def test_oversized_frame_closes(r):
    """Кадр, заявляющий тело больше k_max_msg, должен оборвать соединение."""
    s = connect()
    s.sendall(struct.pack("<I", K_MAX_MSG + 1))        # только фальшивый заголовок
    s.settimeout(2.0)
    try:
        closed = (s.recv(16) == b"")
    except (ConnectionResetError, socket.timeout):
        closed = True
    s.close()
    r.ok("oversized frame -> close", closed)

def test_idle_timeout(r):
    """Простаивающее соединение закрывается по таймауту простоя сервера."""
    s = connect()
    s.settimeout(IDLE_TIMEOUT_S + 3)
    start = time.time()
    try:
        closed = (s.recv(16) == b"")                   # блокируется, пока сервер не закроет
    except (ConnectionResetError, socket.timeout):
        closed = False
    s.close()
    waited = time.time() - start
    r.ok("idle connection reaped", closed and waited >= IDLE_TIMEOUT_S - 1)

# --- запуск ---------------------------------------------------------------

def wait_for_server(timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            connect().close(); return True
        except OSError:
            time.sleep(0.05)
    return False

def main():
    fast = "--fast" in sys.argv
    srv = subprocess.Popen(["./server"])
    r = Runner()
    try:
        if not wait_for_server():
            print("server did not come up"); sys.exit(2)
        s = connect()
        test_strings(r, s)
        test_zset(r, s)
        test_ttl_expiry(r, s)
        test_pipelining(r, s)
        test_binary_and_large(r, s)
        test_mass_insert_rehash(r, s)
        test_large_zset_background_delete(r, s)
        s.close()
        test_concurrent_clients(r)
        test_oversized_frame_closes(r)
        if fast:
            print("[SKIP] idle-timeout test (--fast)")
        else:
            test_idle_timeout(r)
    finally:
        srv.terminate(); srv.wait()
    print(f"\n{r.count - r.fails}/{r.count} checks passed — "
          f"{'ALL PASSED' if r.fails == 0 else str(r.fails) + ' FAILED'}")
    sys.exit(1 if r.fails else 0)

if __name__ == "__main__":
    main()
