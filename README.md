# MESI Coherence Simulator

A C++ simulation of the MESI (Modified, Exclusive, Shared, Invalid) cache coherence protocol for multicore processors. This tool generates synthetic memory access traces to model state transitions and sharer tracking in a directory-based coherence system.

## Project Context
This module serves as the **Logic Phase (Task 2)** for a larger project integrating Reinforcement Learning (RL) agents into Shared Last-Level Cache (LLC) replacement policies using **ChampSim**.

## Features
* **State Machine:** Implements full MESI state transitions.
* **Sharer Tracking:** Simulates a directory controller using sharer sets (prototyping Bitmask logic).
* **Synthetic Trace Generation:** Randomly generates multi-core Read/Write traffic to stress-test coherence invalidations.

## Usage
1. Compile: `g++ mesi_sim.cpp -o mesi_sim`
2. Run: `./mesi_sim`
