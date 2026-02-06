# COALESCE: Coherence-Aware L3 Cache Replacement Policy

> **Bachelor Thesis Project (BTP)** | *Feb 2026* > **Focus:** Systems Architecture, C++, Reinforcement Learning, High-Performance Computing

## Project Overview
**COALESCE** (Coherence-Aware Learning for Shared Cache Efficiency) is a novel cache replacement policy designed for multicore L3 caches. It addresses the "Coherence Wall" in 4-core processors by predicting the **Coherence Cost** of data, not just its recency.

By fusing **Perceptron Learning** with **MESI Coherence States**, this policy protects critical synchronization primitives and shared data that standard policies (LRU, SRRIP) blindly evict.

### Key Performance Highlights
* **Hit Rate:** Achieved **22.34%** in high-contention scenarios (vs. **0.16%** for LRU/SRRIP).
* **Efficiency:** Saved **1,743** costly coherence misses (Interconnect traffic) in synthetic benchmarks.
* **Overhead:** Implemented **3% Set Sampling**, achieving close to 100% accuracy with 97% less storage overhead.

---

## 📂 Repository Structure

This repository is organized to show the evolution of the architecture from concept to C++ implementation.

```text
.
├── simulations/           # Source code for the Cache Simulator
│   ├── coalesce_sim.cpp   # [LATEST] The active 4-Core/8MB simulation engine
│   ├── test               # Compiled executable of the latest engine
│   └── old/               # Archive of previous iterations and experimental logic
│
├── reports/               # Detailed PDF analysis, graphs, and epoch data
│
└── README.md              # Project entry point

```

---

## 🛠️ How to Run the Simulation

The current engine is a standalone C++ event-driven simulator (`coalesce_sim.cpp`). It models a 4-Core, 8MB L3 Cache with **LRU**, **SRRIP**, and **COALESCE** policies running side-by-side.

### 1. Build

Requires a C++ compiler (GCC/Clang). No external dependencies.

```bash
cd simulations
g++ coalesce_sim.cpp -o coalesce_engine -O3

```

### 2. Run

Execute the binary to run the "Scanner vs. Hot-Set" stress test.

```bash
./coalesce_engine

```

### 3. Expected Output

The simulator will output the Hit Rate and Coherence Wins for all three policies, demonstrating the learning curve of the Perceptron over 50 epochs.

---

## Architecture Details

### The "Multiperspective" Brain

Instead of a simple table, COALESCE uses a **Hashed Perceptron** that mixes three architectural features to predict reuse:

1. **Program Counter (PC):** Identifies *who* inserted the data (e.g., distinguishing a "Scanner" instruction from a "Lock" instruction).
2. **Sharer Count:** Identifies *how many* L2 caches hold the line (Safety Veto against evicting shared data).
3. **MESI State:** Identifies *cost* (Prioritizing `MODIFIED` lines to avoid write-backs).

**Formula:**


*See `ARCHITECTURE.md` for the full mathematical derivation.*

---

## Future Roadmap

* **Phase 1 (Complete):** Standalone C++ Simulation & Proof of Concept.
* **Phase 2 (In Progress):** Integration with **ChampSim** for SPEC CPU 2017 benchmarking.
* **Phase 3:** Implementing "Ghost Buffers" for corrective training on premature evictions.

---

*Author: Harsh (Rajharsh)* *Institute: IIIT Sri City*
