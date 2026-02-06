#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <random>
#include <iomanip>

using namespace std;

const int CACHE_SIZE = 32;    // Small cache to force evictions
const int NUM_WAYS = 8;       // 8-way set associative (4 sets)
const int Q_TABLE_SIZE = 128; // Size of PC hash table leads to collision
const double ALPHA = 0.1;     // Learning Rate
const double GAMMA = 0.9;     // Discount Factor
const double EPSILON = 0.1;   // Exploration Rate

struct CacheLine
{
    int tag;
    int last_used;
    bool is_valid;
    int brought_by_pc;
};

struct QEntry
{
    // Q-Value for Action 0
    double q_cache;
    // Q-Value for Action 1
    double q_bypass;
};

class RLAgent
{
    vector<QEntry> q_table;

public:
    RLAgent()
    {
        q_table.resize(Q_TABLE_SIZE, {0.0, 0.0});
    }

    // Hashing the PC to a state index
    int get_state(int pc)
    {
        return pc % Q_TABLE_SIZE;
    }

    // Decide Action: 0 = Cache, 1 = Bypass
    int choose_action(int pc)
    {
        int state = get_state(pc);

        // Epsilon-Greedy Exploration
        if ((double)rand() / RAND_MAX < EPSILON)
        {
            return rand() % 2;
        }

        // Exploitation
        // currently using simple pc-based decision making rather than using neural networks
        if (q_table[state].q_cache >= q_table[state].q_bypass)
            return 0;
        else
            return 1;
    }

    void update(int pc, int action, double reward)
    {
        int state = get_state(pc);
        double &current_q = (action == 0) ? q_table[state].q_cache : q_table[state].q_bypass;

        // Standard Bellman Update (Simplified for single-step cache bandit), will use discount factor in the future
        current_q = current_q + ALPHA * (reward - current_q);
    }

    void print_q_table_head()
    {
        cout << "\n--- Q-Table Snapshot (Top 5 PCs) ---\n";
        cout << "PC_Hash | Q(Cache) | Q(Bypass) | Decision\n";
        for (int i = 0; i < 5; i++)
        {
            string dec = (q_table[i].q_cache >= q_table[i].q_bypass) ? "CACHE" : "BYPASS";
            cout << setw(7) << i << " | "
                 << fixed << setprecision(2) << q_table[i].q_cache << " | "
                 << q_table[i].q_bypass << "    | " << dec << "\n";
        }
        cout << "------------------------------------\n";
    }
};

class CacheSim
{
    vector<vector<CacheLine>> sets;
    int time_step;
    RLAgent agent;

    // final stats
    int hits = 0;
    int misses = 0;

public:
    CacheSim()
    {
        int num_sets = CACHE_SIZE / NUM_WAYS;
        sets.resize(num_sets, vector<CacheLine>(NUM_WAYS, {0, 0, false, 0}));
        time_step = 0;
    }

    void access_memory(int pc, int address)
    {
        time_step++;
        int set_idx = (address >> 6) % sets.size();
        // simplification for uniqueness only remaining bits after offset and index are required
        int tag = address;

        bool hit = false;
        for (auto &line : sets[set_idx])
        {
            if (line.is_valid && line.tag == tag)
            {
                hit = true;
                line.last_used = time_step;
                // rewarding the PC that originally brought this line in.
                agent.update(line.brought_by_pc, 0, +10.0);
                hits++;
                break;
            }
        }

        if (!hit)
        {
            misses++;
            // RL will choose an action based on either exploration(random) or exploitation
            int action = agent.choose_action(pc);

            // bypass
            if (action == 1)
            {
                // (Simplification: Constant small reward for bypass to encourage it for streaming data)
                agent.update(pc, 1, +0.5);
                // In future, we'd penalize if this address is requested again soon.
            }
            else
            {
                // ACTION: CACHE (Insert into Cache)
                // Find Victim (LRU)
                int lru_way = 0;
                int min_time = 999999999;

                for (int i = 0; i < NUM_WAYS; i++)
                {
                    // invalid line contains garbage data anyway and won't be written back to RAM and hence can safely be removed
                    if (!sets[set_idx][i].is_valid)
                    {
                        lru_way = i;
                        break;
                    }
                    if (sets[set_idx][i].last_used < min_time)
                    {
                        min_time = sets[set_idx][i].last_used;
                        lru_way = i;
                    }
                }

                // If we are evicting a valid line that was never reused (Zero-Reuse),
                // we should penalize the PC that brought it in! (future implementation)
                // for now we just insert the new line.
                sets[set_idx][lru_way] = {tag, time_step, true, pc};

                // Initial small penalty for taking up space (Training cost)
                agent.update(pc, 0, -0.1);
            }
        }
    }

    void print_stats()
    {
        cout << "Hits: " << hits << " | Misses: " << misses;
        cout << " | Hit Rate: " << (double)hits / (hits + misses) * 100.0 << "%\n";
        agent.print_q_table_head();
    }
};

int main()
{
    srand(42);
    CacheSim cache;

    cout << "Starting RL-Based Cache Simulation...\n";
    cout << "Scenario: Mixed Workload (Scanning + Looping)\n\n";

    // --- Synthetic Workload Generation ---
    // PC 1: "The Good Loop" (Frequent reuse) -> RL should learn to CACHE
    // PC 2: "The Scanner" (Streaming data, never reused) -> RL should learn to BYPASS
    // epoch is a full pass through training data
    for (int epoch = 0; epoch < 5; epoch++)
    {
        std::cout << "Epoch " << epoch + 1 << ": ";

        for (int i = 0; i < 1000; i++)
        {
            // Pattern 1: Looping (PC 0 accesses addresses 0-10 repeatedly)
            // Ideally, these should stay in cache.
            if (rand() % 2 == 0)
            {
                int addr = rand() % 10;
                cache.access_memory(0, addr); // PC 0
            }

            // Pattern 2: Scanning (PC 4 accesses new random addresses)
            // Ideally, these should NEVER be cached (Pollution).
            else
            {
                int addr = 1000 + i + (epoch * 1000); // Always new address
                cache.access_memory(4, addr);         // PC 4
            }
        }
        cache.print_stats();
    }

    return 0;
}
