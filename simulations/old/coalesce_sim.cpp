#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <random>
#include <bitset>

// --- Configuration ---
const int CACHE_WAYS = 8;         // 8-Way Associativity
const int PERCEPTRON_ROWS = 4096; // Size of weight tables
const int BLOOM_SIZE = 8192;      // Size of Ghost Buffer
const int BYPASS_THRESHOLD = -90; // If vote < -90, don't insert

// --- Helper: Integer Hash Function ---
uint64_t mix_hash(uint64_t a, uint64_t b)
{
    a ^= b + 0x9e3779b9 + (a << 6) + (a >> 2);
    return a % PERCEPTRON_ROWS;
}

// --- The Brain: Adaptive Perceptron Predictor ---
class PerceptronPredictor
{
    // We use TWO tables for Adaptive Hashing
    // Table 0: PC only (General behavior)
    // Table 1: PC + Sharer (Context-specific behavior)
    std::vector<int8_t> table0;
    std::vector<int8_t> table1;

    // Ghost Buffer (Bloom Filter) to track "Mistakes"
    std::bitset<BLOOM_SIZE> ghost_filter;

public:
    PerceptronPredictor()
    {
        table0.resize(PERCEPTRON_ROWS, 0);
        table1.resize(PERCEPTRON_ROWS, 0);
    }

    // GET VOTE: Returns confidence (-128 to 127)
    int get_vote(uint64_t pc, int sharers)
    {
        int idx0 = pc % PERCEPTRON_ROWS;
        int idx1 = mix_hash(pc, sharers);

        // Sum weights from both tables (Adaptive)
        int vote = table0[idx0] + table1[idx1];
        return vote;
    }

    // UPDATE: Train the perceptron
    void train(uint64_t pc, int sharers, bool positive)
    {
        int idx0 = pc % PERCEPTRON_ROWS;
        int idx1 = mix_hash(pc, sharers);

        // Learning Rule: Increment/Decrement with saturation
        if (positive)
        {
            if (table0[idx0] < 127)
                table0[idx0]++;
            if (table1[idx1] < 127)
                table1[idx1]++;
        }
        else
        {
            if (table0[idx0] > -128)
                table0[idx0]--;
            if (table1[idx1] > -128)
                table1[idx1]--;
        }
    }

    // GHOST BUFFER: Insert evicted line
    void insert_ghost(uint64_t address)
    {
        uint64_t h1 = address % BLOOM_SIZE;
        uint64_t h2 = (address / BLOOM_SIZE) % BLOOM_SIZE;
        ghost_filter[h1] = 1;
        ghost_filter[h2] = 1;
    }

    // GHOST BUFFER: Check if we killed this recently
    bool check_ghost(uint64_t address)
    {
        uint64_t h1 = address % BLOOM_SIZE;
        uint64_t h2 = (address / BLOOM_SIZE) % BLOOM_SIZE;
        return ghost_filter[h1] && ghost_filter[h2];
    }
};

// --- The Hardware: Cache Set Simulator ---
struct Line
{
    bool valid;
    uint64_t tag;
    uint64_t pc;   // Signature PC
    int sharers;   // Signature Sharers
    int lru_stack; // 0 = MRU, 7 = LRU
    bool was_used; // did we get a hit?
};

class CacheSet
{
    std::vector<Line> ways;
    PerceptronPredictor *brain; // Pointer to shared predictor

public:
    CacheSet(PerceptronPredictor *p) : brain(p)
    {
        ways.resize(CACHE_WAYS, {false, 0, 0, 0, 7});
        // Initialize LRU stack positions 0,1,2...7
        for (int i = 0; i < CACHE_WAYS; i++)
            ways[i].lru_stack = i;
    }

    // Access Cache
    // Returns: 0=Miss, 1=Hit, 2=Bypassed
    int access(uint64_t address, uint64_t pc, int sharers)
    {

        // 1. CHECK HIT
        for (int i = 0; i < CACHE_WAYS; i++)
        {
            if (ways[i].valid && ways[i].tag == address)
            {
                // HIT!
                update_lru(i);
                ways[i].was_used = true;
                // REWARD: This PC brought useful data!
                brain->train(ways[i].pc, ways[i].sharers, true);
                return 1;
            }
        }

        // 2. MISS - CHECK GHOST BUFFER (Delayed Punishment)
        if (brain->check_ghost(address))
        {
            // We evicted this recently! PUNISH the specific PC/Sharer combo.
            brain->train(pc, sharers, false);
            // Note: In a real sim, we'd punish the PC that *evicted* it,
            // but punishing the PC that *loads* it is a valid proxy for "Hard to cache" lines.
        }

        // 3. DECIDE: BYPASS OR INSERT?
        int vote = brain->get_vote(pc, sharers);

        if (vote < BYPASS_THRESHOLD)
        {
            // BYPASS: Predictor hates this line. Don't cache it.
            // Small reward for bypassing? Maybe not needed if punishment is strong.
            return 2;
        }

        // 4. INSERT (Find Victim)
        int victim_way = find_victim_way();

        // Track the victim in Ghost Buffer before killing it
        if (ways[victim_way].valid)
        {
            brain->insert_ghost(ways[victim_way].tag);
        }

        // 2. PUNISHMENT: If the victim leaves without paying rent (0 hits)
        if (!ways[victim_way].was_used)
        {
            // "PC, you brought this line and nobody used it. Bad!"
            brain->train(ways[victim_way].pc, ways[victim_way].sharers, false);
        }

        // Replace
        ways[victim_way] = {true, address, pc, sharers, 0, false}; // Becomes MRU
        update_lru(victim_way);

        return 0; // Miss
    }

    int find_victim_way()
    {
        // COALESCE Policy:
        // Look at all valid lines. Ask the Brain "Who is most useless?"
        // If Brain is neutral (Vote ~0), fall back to LRU (Stack Position 7).

        int worst_vote = 999;
        int victim_idx = -1;
        int lru_victim = -1;

        for (int i = 0; i < CACHE_WAYS; i++)
        {
            if (!ways[i].valid)
                return i; // Empty slot
            if (ways[i].lru_stack == CACHE_WAYS - 1)
                lru_victim = i;

            int vote = brain->get_vote(ways[i].pc, ways[i].sharers);

            // If the Brain STRONGLY wants to evict (Vote < -20), prioritize it over LRU
            if (vote < worst_vote)
            {
                worst_vote = vote;
                victim_idx = i;
            }
        }

        // Hybrid Decision:
        // If the "worst" line is actually "liked" by the brain (Positive Vote),
        // fallback to standard LRU eviction to be safe.
        if (worst_vote > 0)
            return lru_victim;

        return victim_idx;
    }

    void update_lru(int mru_idx)
    {
        int old_stack_pos = ways[mru_idx].lru_stack;
        for (int i = 0; i < CACHE_WAYS; i++)
        {
            if (ways[i].lru_stack < old_stack_pos)
            {
                ways[i].lru_stack++;
            }
        }
        ways[mru_idx].lru_stack = 0;
    }
};

int main()
{
    PerceptronPredictor brain;
    CacheSet cache(&brain);

    std::cout << "--- Starting COALESCE Standalone Sim ---\n";
    std::cout << "Config: Adaptive Hashing (2-Table), Ghost Buffer, Bypassing\n\n";

    int hits = 0, misses = 0, bypasses = 0;

    // --- SCENARIO: The "Scanner" vs The "Worker" ---
    // PC_SCAN (0xBAD): Scans through array. Sharers=0. (Should Bypass/Evict)
    // PC_WORK (0xF00D): Loops on variable. Sharers=4. (Should Keep)

    for (int epoch = 0; epoch < 10; epoch++)
    {
        for (int i = 0; i < 200; i++)
        {

            // 1. The Good Workload (Repeated Access to Address 50)
            // PC 0xF00D accesses Addr 50. Sharers = 4.
            int res1 = cache.access(50, 0xF00D, 4);
            if (res1 == 1)
                hits++;
            else if (res1 == 2)
                bypasses++;
            else
                misses++;

            // 2. The Bad Workload (Streaming Addresses 100, 101, 102...)
            // PC 0xBAD accesses new Addr every time. Sharers = 0.
            int res2 = cache.access(100 + i + (epoch * 200), 0xBAD, 0);
            if (res2 == 1)
                hits++;
            else if (res2 == 2)
                bypasses++;
            else
                misses++;
        }
    }

    std::cout << "Results:\n";
    std::cout << "Hits: " << hits << "\n";
    std::cout << "Misses: " << misses << "\n";
    std::cout << "Bypasses: " << bypasses << "\n";

    std::cout << "\n--- Brain Inspection ---\n";
    std::cout << "Vote for PC_WORK (0xF00D, Sharers=4): " << brain.get_vote(0xF00D, 4) << " (Should be Positive)\n";
    std::cout << "Vote for PC_SCAN (0xBAD,  Sharers=0): " << brain.get_vote(0xBAD, 0) << " (Should be Negative)\n";

    if (brain.get_vote(0xBAD, 0) < BYPASS_THRESHOLD)
        std::cout << "SUCCESS: The scanner is being bypassed!\n";

    return 0;
}
