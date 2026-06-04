#!/usr/bin/env python3
"""
Нагрузочный тест производительности ООП-сервера кэша «ключ-значение».

В отличие от smoke_test.py (который проверяет *корректность*), здесь измеряется
*производительность* под параллельной нагрузкой на реалистичной модели кэша:

  * множество клиентских соединений, бьющих в цикл событий параллельно,
  * смесь GET/SET (по умолчанию 80/20 — типичный «читающий» веб-профиль),
  * распределение ключей по Zipf, где доминируют несколько «горячих» ключей
    (как в реальном трафике),
  * опциональная модель cache-aside: при промахе GET клиент «подгружает» ключ
    с TTL — ровно так веб-приложение наполняет кэш из своей БД.

Два режима:
  latency     N клиентов в замкнутом цикле (по одному запросу в полёте);
              выводит пропускную способность И перцентили задержки (p50/p90/p99/max).
  throughput  меньше клиентов, каждый конвейеризует P запросов; выводит пиковую
              пропускную способность сервера (задержка тут не показательна).

Примеры:
  python3 bench.py                      # режим latency по умолчанию, 50 клиентов
  python3 bench.py --mode throughput --clients 8 --pipeline 64
  python3 bench.py --ttl-ms 1000 --keyspace 20000   # cache-aside с TTL
"""
import argparse, bisect, itertools, random, socket, struct, subprocess
import sys, threading, time
from array import array

TAG_NIL, TAG_ERR, TAG_STR, TAG_INT, TAG_DBL, TAG_ARR = range(6)


# --- протокол -------------------------------------------------------------

def enc(args):
    body = struct.pack("<I", len(args))
    for a in args:
        a = a if isinstance(a, bytes) else a.encode()
        body += struct.pack("<I", len(a)) + a
    return struct.pack("<I", len(body)) + body

def read_frame(rfile):
    hdr = rfile.read(4)
    if len(hdr) < 4:
        raise EOFError
    (length,) = struct.unpack("<I", hdr)
    return rfile.read(length)

def first_tag(payload):
    return payload[0]


# --- вспомогательное для нагрузки -----------------------------------------

def make_key_sampler(keyspace, zipf):
    """Вернуть f(rnd)->int. zipf<=0 — равномерно; иначе — с перекосом."""
    if zipf <= 0:
        return lambda rnd: rnd.randrange(keyspace)
    weights = [1.0 / ((i + 1) ** zipf) for i in range(keyspace)]
    cum = list(itertools.accumulate(weights))
    total = cum[-1]
    def sample(rnd):
        return bisect.bisect(cum, rnd.random() * total)
    return sample


class Stats:
    def __init__(self):
        self.lat = array("d")     # задержка операции в микросекундах
        self.ops = 0
        self.hits = 0
        self.misses = 0
        self.sets = 0


def percentile(sorted_us, q):
    if not sorted_us:
        return 0.0
    idx = min(len(sorted_us) - 1, int(q * len(sorted_us)))
    return sorted_us[idx]


# --- рабочие потоки -------------------------------------------------------

def latency_worker(args, sampler, n_ops, seed, out, record_lat):
    rnd = random.Random(seed)
    value = b"v" * args.value_size
    st = Stats()
    s = socket.create_connection((args.host, args.port))
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    rfile = s.makefile("rb")
    try:
        for _ in range(n_ops):
            kid = sampler(rnd)
            key = b"k:%d" % kid
            is_get = rnd.random() < args.get_ratio
            t0 = time.perf_counter()
            if is_get:
                s.sendall(enc([b"get", key]))
                tag = first_tag(read_frame(rfile))
                hit = (tag == TAG_STR)
                if hit:
                    st.hits += 1
                else:
                    st.misses += 1
                    if args.ttl_ms > 0:        # подгрузка cache-aside при промахе
                        s.sendall(enc([b"set", key, value]))
                        read_frame(rfile)
                        s.sendall(enc([b"pexpire", key, str(args.ttl_ms)]))
                        read_frame(rfile)
                        st.sets += 1
            else:
                s.sendall(enc([b"set", key, value]))
                read_frame(rfile)
                if args.ttl_ms > 0:
                    s.sendall(enc([b"pexpire", key, str(args.ttl_ms)]))
                    read_frame(rfile)
                st.sets += 1
            dt = (time.perf_counter() - t0) * 1e6
            if record_lat:
                st.lat.append(dt)
            st.ops += 1
    finally:
        rfile.close(); s.close()
    out.append(st)


def throughput_worker(args, sampler, n_ops, seed, out):
    """Конвейерная нагрузка из GET: отправляем P запросов, затем вычитываем P ответов."""
    rnd = random.Random(seed)
    P = args.pipeline
    st = Stats()
    s = socket.create_connection((args.host, args.port))
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    rfile = s.makefile("rb")
    try:
        done = 0
        while done < n_ops:
            batch = min(P, n_ops - done)
            buf = bytearray()
            for _ in range(batch):
                buf += enc([b"get", b"k:%d" % sampler(rnd)])
            s.sendall(buf)
            for _ in range(batch):
                tag = first_tag(read_frame(rfile))
                if tag == TAG_STR:
                    st.hits += 1
                else:
                    st.misses += 1
            st.ops += batch
            done += batch
    finally:
        rfile.close(); s.close()
    out.append(st)


# --- драйвер --------------------------------------------------------------

def prepopulate(args, sampler):
    """Заполнить пространство ключей, чтобы GET попадали (тёплый кэш)."""
    value = b"v" * args.value_size
    s = socket.create_connection((args.host, args.port))
    rfile = s.makefile("rb")
    for kid in range(args.keyspace):
        s.sendall(enc([b"set", b"k:%d" % kid, value]))
        read_frame(rfile)
        if args.ttl_ms > 0:
            s.sendall(enc([b"pexpire", b"k:%d" % kid, str(args.ttl_ms)]))
            read_frame(rfile)
    rfile.close(); s.close()


def run(args):
    sampler = make_key_sampler(args.keyspace, args.zipf)
    print(f"warming up {args.keyspace} keys...", flush=True)
    prepopulate(args, sampler)

    per = args.requests // args.clients
    total = per * args.clients
    results = []
    threads = []

    is_lat = args.mode == "latency"
    worker = (lambda i: latency_worker(args, sampler, per, 1000 + i, results, True)) \
        if is_lat else \
        (lambda i: throughput_worker(args, sampler, per, 1000 + i, results))

    print(f"running {args.mode}: {args.clients} clients x {per} ops "
          f"= {total} ops"
          + (f", pipeline={args.pipeline}" if not is_lat else "")
          + f", get_ratio={args.get_ratio}, zipf={args.zipf}, "
          f"value={args.value_size}B, ttl={args.ttl_ms}ms", flush=True)

    t0 = time.perf_counter()
    for i in range(args.clients):
        t = threading.Thread(target=worker, args=(i,)); t.start(); threads.append(t)
    for t in threads:
        t.join()
    elapsed = time.perf_counter() - t0

    ops = sum(s.ops for s in results)
    hits = sum(s.hits for s in results)
    misses = sum(s.misses for s in results)
    sets = sum(s.sets for s in results)
    gets = hits + misses
    rps = ops / elapsed if elapsed else 0

    print("\n===== RESULTS =====")
    print(f"wall time        : {elapsed:.2f} s")
    print(f"logical ops      : {ops}  (GET {gets}, SET {sets})")
    print(f"throughput       : {rps:,.0f} ops/sec")
    if gets:
        print(f"cache hit ratio  : {100.0*hits/gets:.1f}%  "
              f"(hits {hits}, misses {misses})")
    if is_lat:
        alllat = array("d")
        for s in results:
            alllat.extend(s.lat)
        us = sorted(alllat)
        if us:
            avg = sum(us) / len(us)
            print("latency (per request, microseconds):")
            print(f"  avg {avg:8.1f}   p50 {percentile(us,0.50):8.1f}   "
                  f"p90 {percentile(us,0.90):8.1f}")
            print(f"  p99 {percentile(us,0.99):8.1f}   "
                  f"p999 {percentile(us,0.999):8.1f}   "
                  f"max {us[-1]:8.1f}")
    print("===================")


def main():
    ap = argparse.ArgumentParser(description="Load benchmark for the cache server.")
    ap.add_argument("--mode", choices=["latency", "throughput"], default="latency")
    ap.add_argument("--clients", type=int, default=50)
    ap.add_argument("--requests", type=int, default=200000, help="total ops")
    ap.add_argument("--pipeline", type=int, default=64, help="throughput mode depth")
    ap.add_argument("--get-ratio", type=float, default=0.8)
    ap.add_argument("--keyspace", type=int, default=10000)
    ap.add_argument("--zipf", type=float, default=1.0, help="0 = uniform")
    ap.add_argument("--value-size", type=int, default=64)
    ap.add_argument("--ttl-ms", type=int, default=0, help=">0 enables cache-aside+TTL")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=1234)
    ap.add_argument("--external", action="store_true",
                    help="benchmark an already-running server instead of spawning")
    args = ap.parse_args()

    srv = None
    if not args.external:
        srv = subprocess.Popen(["./server"], stderr=subprocess.DEVNULL)
        time.sleep(0.4)
    try:
        run(args)
    finally:
        if srv:
            srv.terminate(); srv.wait()


if __name__ == "__main__":
    main()
