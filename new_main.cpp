#include <bits/stdc++.h>
using namespace std;

typedef unsigned long long ull;

const int CACHE_SIZE = 32 * 1024;
const int BLOCK_SIZE = 64;
const int ASSOCIATIVITY = 4;

enum Policy {
    LRU,
    FIFO,
    RANDOM,
    BELADY
};

string policyName(Policy p) {
    if (p == LRU) return "LRU";
    if (p == FIFO) return "FIFO";
    if (p == RANDOM) return "RANDOM";
    if (p == BELADY) return "BELADY";
    return "";
}

struct Block {
    bool valid = false;
    ull tag = 0;
    ull block_addr = 0;   //  NEW (important)
    int last_used = 0;
    int inserted_at = 0;
};

int numBlocks = CACHE_SIZE / BLOCK_SIZE;
int numSets = numBlocks / ASSOCIATIVITY;
int offset_bits = log2(BLOCK_SIZE);
int index_bits = log2(numSets);

vector<ull> readTrace(string filename) {
    ifstream file(filename);
    vector<ull> trace;

    string ip, op, addr;
    int size;

    while (file >> ip >> op >> addr >> size) {
        ull address = stoull(addr, nullptr, 16);
        trace.push_back(address);
    }
    return trace;
}

int getLRU(vector<Block>& set) {
    int victim = 0;
    for (int i = 1; i < set.size(); i++)
        if (set[i].last_used < set[victim].last_used)
            victim = i;
    return victim;
}

int getFIFO(vector<Block>& set) {
    int victim = 0;
    for (int i = 1; i < set.size(); i++)
        if (set[i].inserted_at < set[victim].inserted_at)
            victim = i;
    return victim;
}

int getRandom(int ways) {
    return rand() % ways;
}

// FIXED BELADY
int getBelady(vector<Block>& set,
              unordered_map<ull, queue<int>>& future) {

    int victim = -1;
    int farthest = -1;

    for (int i = 0; i < set.size(); i++) {
        ull block_addr = set[i].block_addr;

        if (future[block_addr].empty())
            return i;

        int next_use = future[block_addr].front();

        if (next_use > farthest) {
            farthest = next_use;
            victim = i;
        }
    }
    return victim;
}

void simulate(vector<ull> trace, Policy policy, ofstream& out) {

    vector<vector<Block>> cache(numSets, vector<Block>(ASSOCIATIVITY));

    int hits = 0, misses = 0, evictions = 0;
    int timer = 0;

    unordered_map<ull, queue<int>> future;

    //  Build future using block address
    if (policy == BELADY) {
        for (int i = 0; i < trace.size(); i++) {
            ull block_addr = trace[i] >> offset_bits;
            future[block_addr].push(i);
        }
    }

    for (int i = 0; i < trace.size(); i++) {

        ull addr = trace[i];

        ull block_addr = addr >> offset_bits;   //  important
        int setIndex = block_addr & ((1 << index_bits) - 1);
        ull tag = block_addr >> index_bits;

        auto& set = cache[setIndex];

        timer++;

        if (policy == BELADY)
            future[block_addr].pop();

        bool hit = false;

        for (auto& block : set) {
            if (block.valid && block.tag == tag) {
                hits++;
                hit = true;

                if (policy == LRU)
                    block.last_used = timer;

                break;
            }
        }

        if (!hit) {
            misses++;

            bool placed = false;
            for (auto& block : set) {
                if (!block.valid) {
                    block.valid = true;
                    block.tag = tag;
                    block.block_addr = block_addr; // 
                    block.last_used = timer;
                    block.inserted_at = timer;
                    placed = true;
                    break;
                }
            }

            if (!placed) {
                evictions++;

                int victim;

                if (policy == LRU)
                    victim = getLRU(set);
                else if (policy == FIFO)
                    victim = getFIFO(set);
                else if (policy == RANDOM)
                    victim = getRandom(ASSOCIATIVITY);
                else
                    victim = getBelady(set, future);

                set[victim].tag = tag;
                set[victim].block_addr = block_addr; // 
                set[victim].last_used = timer;
                set[victim].inserted_at = timer;
                set[victim].valid = true;
            }
        }
    }

    double hitRate = (double)hits / trace.size() * 100.0;

    cout << "Policy: " << policyName(policy) << endl;
    cout << "Hits: " << hits << " Misses: " << misses
         << " Evictions: " << evictions << endl;
    cout << "Hit Rate: " << hitRate << "%\n\n";

    out << "Policy: " << policyName(policy) << endl;
    out << "Hits: " << hits << " Misses: " << misses
        << " Evictions: " << evictions << endl;
    out << "Hit Rate: " << hitRate << "%\n\n";
}

int main() {

    srand(time(0));

    vector<ull> trace = readTrace("trace.out");

    ofstream out("statistics.out");

    simulate(trace, LRU, out);
    simulate(trace, FIFO, out);
    simulate(trace, RANDOM, out);
    simulate(trace, BELADY, out);

    out.close();

    cout << "Check statistics.out" << endl;

    return 0;
}
