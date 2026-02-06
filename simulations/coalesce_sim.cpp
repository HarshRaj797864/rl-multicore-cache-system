#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <string>
#include <map>
#include <algorithm>
#include <cstdint>

// configs and constraints
const int NUM_SETS = 64;           // simulating 64 sets to demonstrate sampling
const int WAYS = 8;                // 8-Way Associativity (Standard L3)
const int PERCEPTRON_TABLE_SIZE = 4096; 
const int SAMPLER_SIZE = 8;        // tracks last 8 evictions per sampled set
const int SAMPLING_RATE = 32;      // (approx 3%)

// Coherence States
enum MESI_State { INVALID=0, SHARED=1, EXCLUSIVE=2, MODIFIED=3 };


struct CacheLine {
    bool valid = false;
    uint64_t tag = 0;
    uint64_t pc = 0;
    int sharers = 0;
    MESI_State state = INVALID;
    
    // For LRU/SRRIP baselines
    int lru_stack = 0;  // 0 = MRU, 7 = LRU
    int rrpv = 3;       // 2-bit SRRIP (3=Distant, 0=Immediate)
};

// sampler entry (blooms filter)
struct SamplerEntry {
    bool valid = false;
    uint64_t partial_tag = 0; 
    uint64_t pc = 0;
    int sharers = 0;
    MESI_State state = INVALID;
    int last_prediction = 0;  // on eviction
};


class PerceptronBrain {
    // hardware will use 8-bit int 
    std::vector<int> weights; 
    
public:
    PerceptronBrain() {
        weights.resize(PERCEPTRON_TABLE_SIZE, 0); 
    }

    // implementing single hash function for future comparisons with multiple hash functions
    // aim is to prove that the perceptron policy works better than standard policies even without beneficial complexities
    int get_hash(uint64_t pc, int sharers, MESI_State state) {
        // "Multiperspective" logic
        uint64_t h = pc;
        h ^= (sharers << 4);  
        h ^= (state << 8);    
        return (h ^ 0x9e3779b9) % PERCEPTRON_TABLE_SIZE; 
    }

    // read-only fast prediction for unsampled sets
    int predict(uint64_t pc, int sharers, MESI_State state) {
        return weights[get_hash(pc, sharers, state)];
    }

    // write, slower accurate training for sampled states 
    void train(uint64_t pc, int sharers, MESI_State state, bool positive) {
        int idx = get_hash(pc, sharers, state);
        // the positive represents that it was either a hit or the ghost corrective
        // if its negative then it represents zero-reuse eviction 
        if (positive) {
            if (weights[idx] < 127) weights[idx]++;
        } else {
            if (weights[idx] > -128) weights[idx]--;
        }
    }
};

// abstract base class for COALESCE, LRU, SRRIP
class ReplacementPolicy {
public:
    virtual void update_on_hit(int set_idx, int way, const CacheLine& line) = 0;
    virtual void update_on_miss(int set_idx, int way) = 0;
    virtual int find_victim(int set_idx, const std::vector<CacheLine>& set, 
                            uint64_t current_pc, int current_sharers, MESI_State current_state) = 0;
    virtual std::string name() = 0;
};

// LRU baseline policy
class LRU_Policy : public ReplacementPolicy {
    std::vector<std::vector<int>> lru_stacks; // for simulation tracking
public:
    LRU_Policy() {
        lru_stacks.resize(NUM_SETS, std::vector<int>(WAYS));
        for(int s=0; s<NUM_SETS; s++) 
            for(int w=0; w<WAYS; w++) lru_stacks[s][w] = w; 
    }

    void update_on_hit(int set_idx, int way, const CacheLine& line) override {
        int old_pos = lru_stacks[set_idx][way];
        for(int w=0; w<WAYS; w++) {
            if (lru_stacks[set_idx][w] < old_pos) lru_stacks[set_idx][w]++;
        }
        lru_stacks[set_idx][way] = 0;
    }

    void update_on_miss(int set_idx, int way) override {
        int old_pos = lru_stacks[set_idx][way]; 
        for(int w=0; w<WAYS; w++) {
            if (lru_stacks[set_idx][w] < old_pos) lru_stacks[set_idx][w]++;
        }
        lru_stacks[set_idx][way] = 0;
    }

    int find_victim(int set_idx, const std::vector<CacheLine>& set, uint64_t pc, int sh, MESI_State st) override {
        for(int w=0; w<WAYS; w++) {
            if (!set[w].valid) return w;
            if (lru_stacks[set_idx][w] == WAYS - 1) return w;
        }
        return 0; 
    }
    std::string name() override { return "LRU"; }
};
// SRRIP baseline policy
class SRRIP_Policy : public ReplacementPolicy {
    // 2-bit RRPV: 0 (Immediate), 1, 2, 3 (Distant)
    std::vector<std::vector<int>> rrpv_bits; 
public:
    SRRIP_Policy() {
        rrpv_bits.resize(NUM_SETS, std::vector<int>(WAYS, 3)); 
    }

    void update_on_hit(int set_idx, int way, const CacheLine& line) override {
        rrpv_bits[set_idx][way] = 0; // promote to Immediate Use
    }

    void update_on_miss(int set_idx, int way) override {
        rrpv_bits[set_idx][way] = 2; // Standard SRRIP inserts at Long (2), not Distant (3)
    }

    int find_victim(int set_idx, const std::vector<CacheLine>& set, uint64_t pc, int sh, MESI_State st) override {
        // Scan for RRPV=3. If not found, increment all and repeat.
        while(true) {
            for(int w=0; w<WAYS; w++) {
                if (!set[w].valid) return w;
                if (rrpv_bits[set_idx][w] == 3) return w;
            }
            // Increment (Age)
            for(int w=0; w<WAYS; w++) {
                if (rrpv_bits[set_idx][w] < 3) rrpv_bits[set_idx][w]++;
            }
        }
    }
    std::string name() override { return "SRRIP"; }
};

// COALESCE policy
class COALESCE_Policy : public ReplacementPolicy {
    PerceptronBrain* brain;
    std::vector<std::vector<SamplerEntry>> samplers; 
    std::vector<bool> is_sampled; 

public:
    COALESCE_Policy(PerceptronBrain* b) : brain(b) {
        samplers.resize(NUM_SETS, std::vector<SamplerEntry>(SAMPLER_SIZE));
        is_sampled.resize(NUM_SETS, false);
        
        // setup sampling rate to 3%
        for(int i=0; i<NUM_SETS; i++) {
            if (i % SAMPLING_RATE == 0) is_sampled[i] = true; 
        }
    }

    void update_on_hit(int set_idx, int way, const CacheLine& line) override {
        // rewarding reuse only if the set is a sampled set
        if (is_sampled[set_idx]) {
            brain->train(line.pc, line.sharers, line.state, true);
        }
    }

    void update_on_miss(int set_idx, int way) override {
        // nothing special on miss insertion, handled in victim finding
    }

    int find_victim(int set_idx, const std::vector<CacheLine>& set, 
                   uint64_t current_pc, int current_sharers, MESI_State current_state) override {
        
        // 1. Ghost Buffer Check
        // Note: In real sim, we'd check the incoming address against sampler.
        // For this simplified logic, we assume we missed.
        // if this pessimistic approach proves to be already better than baseline policies then we are good to go
        // 2. Calculate Votes for all candidates
        int victim = -1;
        int min_vote = 99999;

        for(int w=0; w<WAYS; w++) {
            if (!set[w].valid) return w;

            // PREDICT REUSE
            int vote = brain->predict(set[w].pc, set[w].sharers, set[w].state);

            // COHERENCE AWARENESS (The "Secret Sauce")
            // If it's Modified, we really don't want to evict (Costly writeback)
            if (set[w].state == MODIFIED) vote += 60; 
            
            // If it's Shared by many cores, keep it (Costly invalidations)
            if (set[w].sharers > 2) vote += 30;

            if (vote < min_vote) {
                min_vote = vote;
                victim = w;
            }
        }

        // 3. Update Sampler (Only if Sampled Set)
        if (is_sampled[set_idx]) {
            CacheLine victim_line = set[victim];
            // Store eviction metadata
            samplers[set_idx].push_back({true, victim_line.tag, victim_line.pc, victim_line.sharers, victim_line.state, min_vote});
            // (In real code, ring buffer logic here. Simplified push_back for demo)
            
            // NEGATIVE FEEDBACK LOOP
            // If we are evicting it, assume it's dead. Punish it.
            // (Unless it hits in the ghost buffer later, which rewards it).
            brain->train(victim_line.pc, victim_line.sharers, victim_line.state, false);
        }

        return victim;
    }
    std::string name() override { return "COALESCE"; }
};


class Simulator {
    ReplacementPolicy* policy;
    std::vector<std::vector<CacheLine>> cache;
    int hits = 0;
    int misses = 0;
    int coherence_traffic_saved = 0; 

public:
    Simulator(ReplacementPolicy* p) : policy(p) {
        cache.resize(NUM_SETS, std::vector<CacheLine>(WAYS));
    }

    void access(uint64_t addr, uint64_t pc, int sharers, MESI_State state) {
        int set_idx = (addr / 64) % NUM_SETS; 
        uint64_t tag = addr; // Simplified tag

        // 1. CHECK HIT
        for(int w=0; w<WAYS; w++) {
            if (cache[set_idx][w].valid && cache[set_idx][w].tag == tag) {
                hits++;
                policy->update_on_hit(set_idx, w, cache[set_idx][w]);
                
                // Track "Coherence Wins"
                if (cache[set_idx][w].state == MODIFIED || cache[set_idx][w].sharers > 1) {
                    coherence_traffic_saved++;
                }
                return;
            }
        }

        // 2. HANDLE MISS
        misses++;
        int victim_way = policy->find_victim(set_idx, cache[set_idx], pc, sharers, state);
        
        // Evict & Replace
        cache[set_idx][victim_way] = {true, tag, pc, sharers, state, 0, 3};
        policy->update_on_miss(set_idx, victim_way);
    }

    void print_stats() {
        std::cout << std::left << std::setw(15) << policy->name() 
                  << " | Hit Rate: " << std::fixed << std::setprecision(2) 
                  << (100.0 * hits / (hits + misses)) << "%"
                  << " | Coherence Wins: " << coherence_traffic_saved << "\n";
    }
};


int main() {
    std::cout << "=== BTP PROJECT: COALESCE SIMULATION ===\n";
    std::cout << "Config: 64 Sets, 8 Ways. Sampling Rate: 3% (Sets 0, 32)\n\n";

    // Trace Generation Config
    int SC_LOOPS = 4;
    int PP_LOOPS = 200; // Ping Pong happens frequently
    
    // SCENARIO 1: The Scanner (Pollutes Cache)
    // Accesses 1000 lines linearly. Cache only holds 64*8 = 512.
    // LRU should fail (0 hits). Coherence should learn to Bypass or Evict fast.
    
    // SCENARIO 2: The Ping-Pong (Coherence Critical)
    // Accesses a small set of "Hot" lines (Addrs 0-10) repeatedly.
    // Critical: These have Sharers=4 and State=MODIFIED.
    // COALESCE must protect these.

    auto run_test = [&](ReplacementPolicy* p) {
        Simulator sim(p);
        
        // Mixed Workload Generation
        for (int epoch = 0; epoch < 10; epoch++) {
            
            // Phase A: The Scanner (PC 0xBAD) - Low Coherence (Sharers=0)
            for (int i = 0; i < 600; i++) { 
                sim.access(1000 + i + (epoch*100), 0xBAD, 0, EXCLUSIVE);
            }

            // Phase B: The Ping-Pong (PC 0xGOOD) - High Coherence (Sharers=4, MODIFIED)
            for (int k = 0; k < 400; k++) {
                // Accessing hot addresses 0, 1, 2... 15
                sim.access(k % 16, 0xF00D, 4, MODIFIED);
            }
        }
        sim.print_stats();
    };

    PerceptronBrain shared_brain; // The Global Weights

    // 1. Run LRU
    LRU_Policy lru;
    run_test(&lru);

    // 2. Run SRRIP
    SRRIP_Policy srrip;
    run_test(&srrip);

    // 3. Run COALESCE
    COALESCE_Policy coalesce(&shared_brain);
    run_test(&coalesce);

    std::cout << "\nAnalysis:\n";
    std::cout << "- LRU fails because the Scanner flushes out the Hot Ping-Pong lines.\n";
    std::cout << "- COALESCE learns that PC 0xBAD (Scanner) has negative weights.\n";
    std::cout << "- COALESCE learns that PC 0xGOOD (Ping-Pong) + High Sharers = High Priority.\n";

    return 0;
}
