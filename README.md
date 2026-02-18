# PostgreSQL Symmetric Hash Join

## Overview
This project implements a **Symmetric Hash Join (SHJ)** operator inside PostgreSQL, replacing the traditional Hash Join algorithm.

The implementation required modifying both:
- The **Query Optimizer**
- The **Query Executor**

Language: **C**  
Context: **Database Engine Internals**

---

## Motivation

The traditional PostgreSQL Hash Join is a **blocking operator**:
it must build the entire inner hash table before producing any output.

The Symmetric Hash Join removes this bottleneck by:
- Maintaining two hash tables (one per relation)
- Alternating tuple processing between relations
- Producing join results incrementally

This improves pipeline behavior and aligns with demand-pull execution models.

---

## Key Features

- Two hash tables (one for each relation)
- Bidirectional probing
- Fully pipelined tuple-by-tuple execution
- Modified `HashJoinState` to support symmetric execution
- Disabled hybrid batching (single in-memory batch)
- Join result statistics printed after execution

---

## Architecture Changes

### Optimizer (createplan.c)
- Modified plan creation so both inner and outer relations pass through Hash nodes
- Forced symmetric execution structure

### Executor

#### nodeHash.c
- Implemented `ExecHash()` to support pipelined execution
- Disabled multi-batch hybrid hashing
- Enabled incremental tuple insertion

#### nodeHashjoin.c
- Replaced traditional build-then-probe logic
- Implemented alternating tuple processing
- Added bidirectional probing
- Added result counters

#### execnodes.h
- Extended `HashJoinState` structure
- Added state tracking for alternating execution

---

## Execution Model

Instead of:

1. Build inner hash table
2. Probe with outer relation

This implementation performs:

1. Read tuple from T1 → insert into H1 → probe H2  
2. Read tuple from T2 → insert into H2 → probe H1  
3. Repeat until both relations are exhausted

This enables incremental output generation.

---

## Modified Files

- `nodeHashjoin.c`
- `nodeHash.c`
- `execnodes.h`
- `createplan.c`

---

## Technical Concepts

- Query Optimization
- Execution Plan Trees
- Demand-Pull Pipeline Model
- Hash Table Internals
- Database Engine Operator Design
- Systems Programming in C

---

## Academic Context

This project was developed as part of a Database Systems course assignment involving modifications to the PostgreSQL engine.

Based on:
Wilschut & Apers, *Dataflow Query Execution in a Parallel Main-Memory Environment* (PDIS 1991)

---

## Skills Demonstrated

- Low-level C programming
- Database engine internals
- Query execution algorithms
- Stateful operator implementation
- Systems-level debugging
