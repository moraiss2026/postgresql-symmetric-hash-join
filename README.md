Implementation of a Symmetric Hash Join operator inside PostgreSQL.

This project replaces the traditional Hash Join with a fully pipelined Symmetric Hash Join algorithm by modifying both the PostgreSQL optimizer and executor components.

Key modifications:
- Bidirectional hash probing
- Pipelined hash node execution
- Removal of batch-based hybrid hashing
- Custom HashJoinState redesign

Language: C  
System-level database engine modification
