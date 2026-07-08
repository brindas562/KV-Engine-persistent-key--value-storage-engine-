# KVEngine

A persistent key-value storage engine written in C++17, inspired by the [Bitcask](https://riak.com/assets/bitcask-intro.pdf) paper. Built from scratch as a systems programming project — no external libraries, no frameworks.

---

## Architecture

```
  Client (TCP)
      │
      ▼
  Server (port 8080)
      │  one thread per connection
      ▼
  KVStore
      │
      ├── memTable (unordered_map, in-memory write buffer)
      │       │
      │       │  flush at 10,000 keys
      │       ▼
      ├── SSTable files  (data/sstable_N.dat)
      │       │
      │       │  per-SSTable in-memory structures:
      │       ├── Bloom Filter     ← probabilistic gate, eliminates I/O on misses
      │       └── Sparse Index     ← binary search to nearest block offset
      │
      ├── WAL  (data/data.log)     ← append-only write-ahead log
      │
      └── KeyDir (unordered_map)   ← key → WAL offset, rebuilt on restart
```

**Read path (GET):**
1. Check memTable — O(1) hash lookup, no I/O
2. For each SSTable (newest → oldest):
   - Skip if key < minKey or key > maxKey
   - **Bloom filter check** — if "definitely not", skip entirely (no file I/O)
   - Binary search sparse index → seek to nearest block → linear scan
3. Fall back to WAL via KeyDir offset

**Write path (SET):**
1. Append to WAL (binary format: keyLen | valLen | key | value)
2. Update KeyDir and memTable
3. If memTable hits 10,000 keys → flush to a new SSTable

**Delete (DEL):** writes a `__TOMBSTONE__` value — logically removes the key without touching existing files.

---

## File Format

**WAL / SSTable record layout (binary):**

```
[ uint32 keyLen ][ uint32 valLen ][ key bytes ][ value bytes ]
```

Both the WAL (`data/data.log`) and SSTable files (`data/sstable_N.dat`) use this same format. SSTable files are written with keys in sorted order, enabling the sparse index and range-check optimizations.

---

## Bloom Filter

Each SSTable carries an in-memory bloom filter built at flush time (and rebuilt on restart by scanning the file). It uses **double hashing** (Kirsch-Mitzenmacher technique):

```
g_i(key) = h1(key) + i * h2(key)   for i in [0, k)
```

- `h1` — FNV-1a 64-bit
- `h2` — djb2 64-bit (odd-forced, coprime-safe)
- `m` (bit count) and `k` (hash count) computed from expected key count at 1% false-positive rate

Before any SSTable file is opened, `mightContain(key)` is called. A `false` answer is a **guarantee** the key is absent — the file is skipped entirely. This eliminates disk I/O for missing keys, which is the common case in real workloads.

---

## Sparse Index

Every 100th key written to an SSTable is recorded as `(key, file_offset)`. On a GET:

1. Binary search the index for the largest entry ≤ target key
2. Seek to that file offset
3. Linear scan forward until key matches or key exceeds target

This bounds the scan to ~100 records per lookup regardless of SSTable size, without storing an index entry per key.

---

## Compaction

When 10 SSTables accumulate, `compactSSTables()` merges them into one:
- Loads all files in order (older → newer), so newer values overwrite older ones
- Tombstoned keys are dropped during merge
- Writes a single new SSTable, deletes the old files
- Rebuilds bloom filter and sparse index for the merged file

---

## Commands

The server speaks a plain-text protocol over TCP (port 8080):

| Command | Example | Response |
|---|---|---|
| `SET key value` | `SET name alice` | `OK` |
| `GET key` | `GET name` | `alice` or `NOT_FOUND` |
| `DEL key` | `DEL name` | `OK` or `NOT_FOUND` |
| `STATS` | `STATS` | hit/miss counters |

---

## Build

**Requirements:** GCC 9+ or Clang 10+, C++17, POSIX (Linux / macOS)

**Build the server:**
```bash
g++ -std=c++17 -pthread -O2 \
    src/kvstore.cpp src/sstable.cpp src/command_parser.cpp src/server.cpp src/main.cpp \
    -Iinclude -o kvserver
```

**Run the server:**
```bash
mkdir -p data
./kvserver
# Server listening on 8080
```

**Build and run the client:**
```bash
g++ -std=c++17 -o client client/client.cpp
./client
```

**Build and run all storage tests (no server needed):**
```bash
g++ -std=c++17 -pthread -O2 \
    src/kvstore.cpp src/sstable.cpp src/command_parser.cpp \
    benchmark/full_storage_test.cpp \
    -Iinclude -o storage_test
./storage_test
```

**Build and run the benchmark client (server must be running):**
```bash
g++ -std=c++17 -pthread -O2 \
    benchmark/benchmark_client_v1.cpp \
    -o benchmark_client
./benchmark_client
# 100 concurrent clients, 300s run, SET/GET/DEL mix
```

---

## Project Structure

```
KVS/
├── include/
│   ├── bloom_filter.h      # Bloom filter (double hashing, no dependencies)
│   ├── kvstore.h           # KVStore class + SSTableMeta struct
│   ├── sstable.h           # SSTable read/write/index utilities
│   ├── server.h            # TCP server
│   └── command.h           # Command struct
├── src/
│   ├── kvstore.cpp         # Core storage engine
│   ├── sstable.cpp         # SSTable + bloom filter implementation
│   ├── server.cpp          # TCP server + command dispatch
│   ├── command_parser.cpp  # Text protocol parser
│   └── main.cpp            # Entry point (port 8080)
├── benchmark/
│   ├── full_storage_test.cpp    # 13 functional tests (no server needed)
│   ├── benchmark_client_v1.cpp  # 100-client concurrent benchmark
│   ├── benchmark_client.cpp     # Single-client benchmark
│   └── sstable_test.cpp         # SSTable unit tests
└── client/
    └── client.cpp          # Interactive TCP client
```

---

## Tests

13 functional tests covering the full storage engine lifecycle:

```
TEST 1  — Basic SET / GET
TEST 2  — DELETE (tombstone)
TEST 3  — WAL recovery after restart
TEST 4  — SSTable flush
TEST 5  — Restart after SSTable flush
TEST 6  — Update existing key
TEST 7  — Delete existing key
TEST 8  — Newest SSTable wins (read ordering)
TEST 9  — Compaction trigger (10 SSTables → 1)
TEST 10 — Restart after compaction
TEST 11 — Massive dataset (50,000+ keys)
TEST 12 — Restart after massive dataset
TEST 13 — Random verification (1,000 key sample)
```

All 13 tests pass. Each prints a hit/miss breakdown showing which layer (memTable / SSTable / WAL) served the read.

---

## Key Design Decisions

**Why Bitcask-style WAL instead of a B-tree?**
Appending to the WAL is a single sequential write — no page splits, no rebalancing. Reads go through the in-memory KeyDir (O(1) lookup) followed by a single seek. This gives write throughput close to raw disk speed.

**Why a sparse index instead of a full index?**
A full index stores one entry per key — for 10,000 keys per SSTable that's 10,000 entries in memory. A sparse index (every 100th key) stores 100 entries with at most 100 records of linear scan per lookup. Same O(1) amortized cost, ~100x less memory.

**Why double hashing for the bloom filter?**
Kirsch-Mitzenmacher (2006) proves that two hash functions are sufficient to simulate k independent hashes with the same false-positive bound. FNV-1a and djb2 are different hash families, fast to compute, and produce well-distributed outputs on string keys.

**Why text protocol instead of binary?**
Simplicity for a portfolio project. A production system would use a length-prefixed binary frame (4-byte keyLen + 4-byte valLen + payload) to handle values with spaces and remove the 1024-byte buffer limit.

---

## Concurrency Model

- `shared_mutex` — multiple concurrent readers, exclusive writers
- `shared_lock` on GET, `unique_lock` on SET/DEL/flush
- Background `flushWorker` thread group-commits the WAL every 10ms
- One thread per TCP client connection (detached)

## Future Enhancements

Benchmark numbers. You have a benchmark client already (benchmark_client_v1.cpp, 100 concurrent clients, 300s run) — run it and put throughput/latency numbers (ops/sec, p99 latency) in your resume bullet. Right now the repo has the tool but the README doesn't report results. This is the single highest-leverage improvement.

Crash-consistency detail. You mention WAL recovery in tests — if you can say "recovers full state after crash by replaying WAL" that's a strong line, since durability guarantees are a common systems interview follow-up.
Scale claim. Test 11 mentions 50,000+ keys — could quantify SSTable count, compaction behavior at scale, or memory footprint of bloom filters/sparse index.