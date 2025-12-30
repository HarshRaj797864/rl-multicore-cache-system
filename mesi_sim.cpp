/**
 * MESI Coherence Simulator for Multicore Architectures
 * * This simulation models a Directory-Based Cache Coherence protocol.
 * It tracks the state of cache lines (Modified, Exclusive, Shared, Invalid)
 * and maintains a 'Sharer Bitmask' to track which cores hold copies of data.
 * * Future Integration: This logic will be ported to ChampSim's LLC replacement policy
 * to provide features for an RL-based replacement agent.
 */

#include <iostream>
#include <vector>
#include <random>
#include <set>
#include <iomanip>
#include <string>

// Configs
const int NUM_CORES = 4;
// setting small memory space to force cache collisions
const int MEMORY_SIZE = 16;

using namespace std;

// MESI states
enum class State
{
    MODIFIED,
    EXCLUSIVE,
    SHARED,
    INVALID
};
// helper functions
string stateToString(State s)
{
    switch (s)
    {
    case State::MODIFIED:
        return "MODIFIED";
    case State::EXCLUSIVE:
        return "EXCLUSIVE";
    case State::SHARED:
        return "SHARED";
    case State::INVALID:
        return "INVALID";
    default:
        return "UNKNOWN";
    }
}

struct DirectoryEntry
{
    State state;
    // list of cores equivalent to SHARER BITMASK
    set<int> sharers;
    // Initializing every core to safest INVALID state
    DirectoryEntry() : state(State::INVALID) {}
};

class CoherenceController
{
    vector<DirectoryEntry> directory;

public:
    CoherenceController()
    {
        directory.resize(MEMORY_SIZE);
    }

    void handleReadRequest(int coreId, int address)
    {
        DirectoryEntry &line = directory[address];
        cout << "[READ] Core " << coreId << " -> Addr " << address << " | ";

        if (line.state == State::INVALID)
        {
            // if no other core is actively accessing this line then change its state to Exclusive for directory
            line.state = State::EXCLUSIVE;
            line.sharers.insert(coreId);
            // the directory has to go to RAM and bring the line to cache
            cout << "Miss: Granting EXCLUSIVE" << endl;
        }
        else if (line.state == State::EXCLUSIVE || line.state == State::MODIFIED)
        {
            // some other core has the line privately downgrade them to share
            line.state = State::SHARED;
            line.sharers.insert(coreId);
            // here cache-to-cache transfer happens
            cout << "Hit (Private). Downgrading owner to SHARED." << endl;
        }
        else if (line.state == State::SHARED)
        {
            line.sharers.insert(coreId);
            cout << "Hit (Shared). Adding to sharers list." << endl;
        }
        printDebugInfo(line);
    }

    void handleWriteRequest(int coreId, int address)
    {
        DirectoryEntry &line = directory[address];
        cout << "[WRITE] Core " << coreId << " -> Addr " << setw(2) << address << " | ";

        // writes must be exclusive hence all other cores must be invalidated
        if (!line.sharers.empty())
        {
            if (line.sharers.size() > 1 || *line.sharers.begin() != coreId)
            {
                cout << "Invalidating " << line.sharers.size() << " other cores. ";
            }
        }
        line.sharers.clear();
        line.sharers.insert(coreId);
        line.state = State::MODIFIED;
        cout << "Granting MODIFIED ownership." << endl;
        printDebugInfo(line);
    }

    void printDebugInfo(const DirectoryEntry &line)
    {
        cout << "--- Directory Entry Check ---" << endl;

        cout << "State: ";
        if (line.state == State::MODIFIED)
            cout << "M";
        else if (line.state == State::EXCLUSIVE)
            cout << "E";
        else if (line.state == State::SHARED)
            cout << "S";
        else
            cout << "I";
        cout << endl;

        cout << "Sharers: { ";
        for (int core_id : line.sharers)
        {
            cout << core_id << " ";
        }
        cout << "}" << endl;

        cout << "Sharer Count: " << line.sharers.size() << endl;
        cout << "----------------------------" << endl;
    }
};

int main() {
    CoherenceController sim;
    // seeding
    random_device rd;
    mt19937 gen(rd());
    // picking a random core
    uniform_int_distribution<> coreDist(0, NUM_CORES - 1);
    // piking a random memory address
    uniform_int_distribution<> addrDist(0, MEMORY_SIZE - 1);
    uniform_int_distribution<> opDist(0, 1); // 0 = Read, 1 = Write

    cout << "========================================================\n";
    cout << "   MESI Coherence Simulator (Synthetic Trace Generator) \n";
    cout << "========================================================\n\n";

    for (int i = 0; i < 15; ++i) {
        int core = coreDist(gen);
        int addr = addrDist(gen);
        int op = opDist(gen);

        if (op == 0) sim.handleReadRequest(core, addr);
        else         sim.handleWriteRequest(core, addr);
    }
    return 0;
}
