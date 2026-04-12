# Memory-Cache-Simulator
Project Overview: This project focuses on building a highly configurable, trace-driven memory cache simulator. The primary goal is to evaluate how different cache configurations and replacement policies handle real-world workload traces.

Pintool_details.pdf contains the optimization that this pintool brings in details :
by using :
Buffered File Writes : 
Batched 10,000 trace entries before writing to disk
Reduced I/O operations by ~10,000×
Achieved ~99.99% reduction in disk overhead

Struct Packing (#pragma pack(1)) : 
Reduced trace entry size from 24 bytes → 17 bytes
Saved ~29% memory (~70 MB for 10M entries)
Improved disk throughput due to smaller writes

Instruction Filtering (Main Executable Only) : 
Ignored library/system instructions
Reduced instrumentation workload by ~70%
Significantly lowered runtime overhead

Use of unordered_map (Hash Maps) :
Achieved average O(1) lookup time
~20× faster than tree-based map for large datasets
Enabled efficient real-time tracking of memory accesses

Inline Temporal Distance Computation :
Calculated reuse distance during execution
Eliminated need for post-processing
Reduced memory usage by ~80–90%

Heap-Based Top-K Extraction : 
Used priority queue instead of full sorting
Reduced complexity from O(N log N) → O(N log K)
Achieved ~8–10× faster top-address identification
