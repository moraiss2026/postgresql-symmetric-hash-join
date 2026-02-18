PostgreSQL Symmetric Hash Join
Overview

This project implements a Symmetric Hash Join (SHJ) operator inside PostgreSQL, replacing the traditional Hash Join algorithm.

The implementation modifies both:

Query Optimizer

Query Executor

Motivation

Traditional Hash Join is blocking: it must build the entire inner hash table before producing output.
Symmetric Hash Join removes this bottleneck by incrementally processing both relations.

Key Features

Two hash tables (one per relation)

Alternating tuple processing

Fully pipelined execution

Modified HashJoinState for bidirectional probing

Disabled hybrid batching for simplified memory model

Modified Files

nodeHashjoin.c

nodeHash.c

execnodes.h

createplan.c

Technologies

C

PostgreSQL internals

Query execution engine modification
