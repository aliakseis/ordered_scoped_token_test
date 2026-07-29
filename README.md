# Ordered Scoped Token Generator

A lightweight, RAII-based synchronization primitive that guarantees **strict FIFO execution order** while automatically handling cancellation and cleanup.

Unlike a traditional mutex, every caller receives its own **one-shot token** representing a unique position in an execution sequence. A token can either:

- wait until it becomes its turn and enter the protected scope;
- attempt a non-blocking acquisition;
- or simply be destroyed, automatically allowing later tokens to proceed.

The implementation is entirely RAII-based: no explicit release or cancellation API is required.

---

# Motivation

Many synchronization problems require more than mutual exclusion.

For example:

- processing work items in submission order;
- serializing access to a non-thread-safe resource;
- preserving ordering between asynchronous operations;
- guaranteeing deterministic execution in producer/consumer pipelines.

A regular mutex only guarantees **mutual exclusion**.

It does **not** guarantee that waiting threads acquire the mutex in the same order they requested it.

`OrderedScopedTokenGenerator` solves this problem by assigning every operation a monotonically increasing sequence number.

Only the operation owning the current sequence number may enter the critical section.

---

# Features

- Strict FIFO ordering
- RAII lifetime management
- Automatic cancellation of discarded tokens
- Automatic advancement when scopes exit
- Move-only semantics
- Exception safe
- Thread safe
- Supports blocking and non-blocking acquisition
- No manual cleanup required
- Deterministic execution order

---

# Design Overview

Each generated token receives an internal sequence index.

```
Token #0
Token #1
Token #2
Token #3
...
```

The generator maintains two counters:

- `alloc_index` — next sequence number to assign.
- `next_index` — sequence number currently allowed to execute.

Only the token whose index equals `next_index` may enter the protected scope.

---

# Object Lifetime

A token owns an internal `State` object.

```
Token
 │
 ▼
State
```

Calling `lock()` transfers ownership to a `LockScope`.

```
Token
    lock()

        │
        ▼

LockScope
     │
     ▼
    State
```

Destroying either:

- an unused token, or
- an active lock scope

destroys the same `State` object.

Its destructor automatically advances the generator.

```
State::~State()
        │
        ▼
consume_and_advance()
        │
        ▼
next waiting thread
```

This completely eliminates the need for explicit unlock or cancel operations.

---

# Basic Usage

```cpp
OrderedScopedTokenGenerator generator;

auto token = generator.generate();

{
    auto lock = token.lock();

    // protected code
}

// automatically released here
```

---

# Ordered Execution

```
Thread A      Thread B      Thread C

Token 0
Token 1
Token 2

    │
    ▼

Token 0 executes

    │
    ▼

Token 1 executes

    │
    ▼

Token 2 executes
```

Execution order is determined solely by token creation order.

Thread scheduling does not affect execution order.

---

# Automatic Cancellation

Unused tokens do not block the queue forever.

Example:

```cpp
auto t0 = generator.generate();
auto t1 = generator.generate();
auto t2 = generator.generate();

auto scope = t0.lock();

t1 = {};
```

Destroying `t1` automatically marks its sequence number as consumed.

When `scope` exits, execution proceeds directly to `t2`.

No explicit cancellation is necessary.

---

# Non-Blocking Acquisition

```cpp
auto token = generator.generate();

auto scope = token.try_lock();

if (scope.valid())
{
    // acquired immediately
}
else
{
    // not our turn yet
}
```

---

# Move Semantics

Both `Token` and `LockScope` are move-only.

```cpp
auto token = generator.generate();

auto other = std::move(token);
```

This guarantees unique ownership of every sequence position.

---

# Internal Algorithm

When a token is created:

```
index = alloc_index++
```

When `lock()` succeeds:

```
wait until

index == next_index
```

Destroying the associated `State` performs:

```
if index == next_index

    next_index++

    while(next_index already consumed)
        next_index++
else
    remember index as consumed
```

Finally, waiting threads are notified.

---

# Complexity

| Operation | Complexity |
|----------|-----------:|
| Generate token | O(1) |
| Destroy token | O(1) amortized |
| Lock | O(1) |
| Unlock | O(1) amortized |
| Memory | O(number of skipped tokens) |

---

# Thread Safety

The implementation uses:

- `std::mutex`
- `std::condition_variable`

All shared state is protected by the generator's mutex.

Multiple threads may safely:

- generate tokens,
- wait,
- cancel,
- release

concurrently.

---

# Exception Safety

The implementation provides strong RAII guarantees.

If a token is destroyed because of:

- an exception,
- early return,
- scope exit,

its sequence number is automatically consumed.

This prevents deadlocks caused by forgotten unlock or cancellation operations.

---

# Typical Applications

- Ordered task execution
- Serializing asynchronous callbacks
- Ordered logging
- Network packet processing
- Database transaction sequencing
- Media processing pipelines
- GUI command serialization
- Producer/consumer ordering
- Deterministic testing
- Resource access scheduling

---

# Comparison

| Feature | `std::mutex` | Ordered Scoped Token |
|---------|--------------|---------------------|
| Mutual exclusion | ✔ | ✔ |
| FIFO ordering | ✖ | ✔ |
| Automatic cancellation | ✖ | ✔ |
| RAII unlock | ✔ | ✔ |
| One-shot ownership | ✖ | ✔ |
| Deterministic execution | ✖ | ✔ |

---

# Implementation Notes

The implementation stores skipped sequence numbers inside an internal set.

When the current sequence finishes, consecutive consumed indices are coalesced automatically:

```
next = 5

consumed:
6
7
8

↓

release token 5

↓

next becomes 9
```

This allows discarded tokens to be skipped efficiently while preserving strict ordering.

---

# Summary

`OrderedScopedTokenGenerator` is a small synchronization utility that combines:

- strict FIFO ordering,
- scoped RAII lifetime management,
- automatic cancellation,
- deterministic execution,
- and exception safety

into a simple move-only API.

By associating progression with object lifetime rather than explicit API calls, the implementation naturally avoids forgotten releases, leaked sequence numbers, and many classes of synchronization errors while remaining compact and efficient.
