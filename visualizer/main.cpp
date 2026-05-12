#include <bits/stdc++.h>
using namespace std;

using ull = unsigned long long;

enum Policy {
    LRU,
    FIFO,
    RANDOM,
    BELADY,
    SRRIP,
    HAWKEYE,
    DRP
};

const vector<Policy> ALL_POLICIES = {LRU, FIFO, RANDOM, BELADY, SRRIP, HAWKEYE, DRP};
const vector<string> POLICY_NAMES = {"LRU", "FIFO", "RANDOM", "BELADY", "SRRIP", "HAWKEYE", "DRP"};

string policyName(Policy policy) {
    return POLICY_NAMES[(int)policy];
}

struct Access {
    ull pc = 0;
    ull addr = 0;
    char op = '-';
    int size = 0;
};

struct CacheConfig {
    string name;
    int cacheSizeBytes;
    int blockSizeBytes;
    int associativity;
};

struct Block {
    bool valid = false;
    ull tag = 0;
    ull blockAddress = 0;
    long long lastUsed = 0;
    long long insertTime = 0;
    int rrpv = 2;
    ull insertingPc = 0;
    bool gotHit = false;
    bool isStable = false;
};

struct Stats {
    long long accesses = 0;
    long long loads = 0;
    long long stores = 0;
    long long hits = 0;
    long long misses = 0;
    long long evictions = 0;

    double hitRate() const {
        return accesses ? (double)hits * 100.0 / accesses : 0.0;
    }

    double missRate() const {
        return accesses ? (double)misses * 100.0 / accesses : 0.0;
    }
};

struct SimulationResult {
    Policy policy = LRU;
    CacheConfig l1Config;
    CacheConfig l2Config;
    Stats l1;
    Stats l2;
    long long traceAccesses = 0;
    long long memoryAccesses = 0;

    long long totalCacheHits() const {
        return l1.hits + l2.hits;
    }

    double overallHitRate() const {
        return traceAccesses ? (double)totalCacheHits() * 100.0 / traceAccesses : 0.0;
    }

    double memoryAccessRate() const {
        return traceAccesses ? (double)memoryAccesses * 100.0 / traceAccesses : 0.0;
    }
};

vector<Access> readTrace(const string& filename) {
    ifstream file(filename);
    vector<Access> trace;

    string ip, op, addr;
    int size;

    while (file >> ip >> op >> addr >> size) {
        Access access;
        access.pc = stoull(ip, nullptr, 16);
        access.addr = stoull(addr, nullptr, 16);
        access.op = op.empty() ? '-' : op[0];
        access.size = size;
        trace.push_back(access);
    }

    return trace;
}

void countOperation(Stats& stats, char op) {
    if (op == 'L' || op == 'R')
        stats.loads++;
    else if (op == 'S' || op == 'W')
        stats.stores++;
}

class CacheLevel {
private:
    CacheConfig config;
    int numBlocks = 0;
    int numSets = 0;
    vector<vector<Block>> cache;
    Stats stats;
    unordered_map<ull, int> hawkeyePred;
    deque<ull> victimBuffer;
    int maxRegretSize = 4;

    struct DecodedAddress {
        ull blockAddress;
        int setIndex;
        ull tag;
    };

    DecodedAddress decode(ull address) const {
        ull blockAddress = address / config.blockSizeBytes;
        int setIndex = (int)(blockAddress % numSets);
        ull tag = blockAddress / numSets;
        return {blockAddress, setIndex, tag};
    }

    int getLRU(const vector<Block>& set) const {
        int victim = 0;
        for (int i = 1; i < (int)set.size(); i++) {
            if (set[i].lastUsed < set[victim].lastUsed)
                victim = i;
        }
        return victim;
    }

    int getFIFO(const vector<Block>& set) const {
        int victim = 0;
        for (int i = 1; i < (int)set.size(); i++) {
            if (set[i].insertTime < set[victim].insertTime)
                victim = i;
        }
        return victim;
    }

    int getRandom() const {
        return rand() % config.associativity;
    }

    int getBelady(const vector<Block>& set,
                  const unordered_map<ull, vector<int>>& future,
                  int currentIndex) const {
        int victim = 0;
        int farthest = -1;

        for (int i = 0; i < (int)set.size(); i++) {
            auto it = future.find(set[i].blockAddress);
            if (it == future.end())
                return i;

            const vector<int>& positions = it->second;
            auto next = upper_bound(positions.begin(), positions.end(), currentIndex);
            if (next == positions.end())
                return i;

            if (*next > farthest) {
                farthest = *next;
                victim = i;
            }
        }

        return victim;
    }

    int getSRRIP(vector<Block>& set) {
        while (true) {
            for (int i = 0; i < (int)set.size(); i++) {
                if (set[i].rrpv == 3)
                    return i;
            }
            for (Block& block : set) {
                if (block.rrpv < 3)
                    block.rrpv++;
            }
        }
    }

    int getDRP(const vector<Block>& set) const {
        int oldestUnstable = -1;
        int oldestStable = -1;

        for (int i = 0; i < (int)set.size(); i++) {
            if (!set[i].isStable) {
                if (oldestUnstable == -1 || set[i].lastUsed < set[oldestUnstable].lastUsed)
                    oldestUnstable = i;
            } else {
                if (oldestStable == -1 || set[i].lastUsed < set[oldestStable].lastUsed)
                    oldestStable = i;
            }
        }

        if (oldestUnstable != -1)
            return oldestUnstable;
        return oldestStable == -1 ? 0 : oldestStable;
    }

    int chooseVictim(Policy policy,
                     vector<Block>& set,
                     const unordered_map<ull, vector<int>>& future,
                     int currentIndex) {
        if (policy == LRU || policy == HAWKEYE) return getLRU(set);
        if (policy == FIFO) return getFIFO(set);
        if (policy == RANDOM) return getRandom();
        if (policy == SRRIP) return getSRRIP(set);
        if (policy == DRP) return getDRP(set);
        return getBelady(set, future, currentIndex);
    }

public:
    explicit CacheLevel(CacheConfig cfg) : config(std::move(cfg)) {
        numBlocks = config.cacheSizeBytes / config.blockSizeBytes;
        numSets = numBlocks / config.associativity;

        if (config.cacheSizeBytes <= 0 ||
            config.blockSizeBytes <= 0 ||
            config.associativity <= 0 ||
            config.cacheSizeBytes % config.blockSizeBytes != 0 ||
            numBlocks % config.associativity != 0 ||
            numSets <= 0) {
            throw runtime_error("Invalid cache configuration for " + config.name);
        }

        cache.assign(numSets, vector<Block>(config.associativity));
    }

    unordered_map<ull, vector<int>> buildFuture(const vector<Access>& trace) const {
        unordered_map<ull, vector<int>> future;
        for (int i = 0; i < (int)trace.size(); i++) {
            ull blockAddress = trace[i].addr / config.blockSizeBytes;
            future[blockAddress].push_back(i);
        }
        return future;
    }

    bool access(const Access& access, Policy policy, long long timer) {
        DecodedAddress decoded = decode(access.addr);
        vector<Block>& set = cache[decoded.setIndex];

        stats.accesses++;
        countOperation(stats, access.op);

        for (Block& block : set) {
            if (block.valid && block.tag == decoded.tag) {
                stats.hits++;

                if (policy == LRU || policy == HAWKEYE || policy == DRP)
                    block.lastUsed = timer;
                if (policy == SRRIP)
                    block.rrpv = 0;
                if (policy == HAWKEYE)
                    block.gotHit = true;

                return true;
            }
        }

        stats.misses++;
        return false;
    }

    void insert(const Access& access,
                Policy policy,
                long long timer,
                const unordered_map<ull, vector<int>>& future,
                int currentIndex) {
        DecodedAddress decoded = decode(access.addr);
        vector<Block>& set = cache[decoded.setIndex];

        for (Block& block : set) {
            if (block.valid && block.tag == decoded.tag) {
                if (policy == LRU || policy == HAWKEYE || policy == DRP)
                    block.lastUsed = timer;
                if (policy == SRRIP)
                    block.rrpv = 0;
                return;
            }
        }

        int targetIndex = -1;
        for (int i = 0; i < (int)set.size(); i++) {
            if (!set[i].valid) {
                targetIndex = i;
                break;
            }
        }

        if (targetIndex == -1) {
            stats.evictions++;
            targetIndex = chooseVictim(policy, set, future, currentIndex);

            if (policy == HAWKEYE) {
                ull oldPc = set[targetIndex].insertingPc;
                if (set[targetIndex].gotHit)
                    hawkeyePred[oldPc] = min(7, hawkeyePred[oldPc] + 1);
                else
                    hawkeyePred[oldPc] = max(0, hawkeyePred[oldPc] - 1);
            } else if (policy == DRP) {
                victimBuffer.push_back(set[targetIndex].blockAddress);
                if ((int)victimBuffer.size() > maxRegretSize)
                    victimBuffer.pop_front();
            }
        }

        Block& block = set[targetIndex];
        block.valid = true;
        block.tag = decoded.tag;
        block.blockAddress = decoded.blockAddress;
        block.insertTime = timer;

        if (policy == SRRIP) {
            block.rrpv = 2;
            block.lastUsed = timer;
        } else if (policy == HAWKEYE) {
            block.insertingPc = access.pc;
            block.gotHit = false;

            if (hawkeyePred.find(access.pc) == hawkeyePred.end())
                hawkeyePred[access.pc] = 4;

            block.lastUsed = hawkeyePred[access.pc] >= 4 ? timer : -1;
        } else if (policy == DRP) {
            block.lastUsed = timer;
            block.isStable = false;
            for (ull oldBlock : victimBuffer) {
                if (oldBlock == decoded.blockAddress) {
                    block.isStable = true;
                    break;
                }
            }
        } else {
            block.lastUsed = timer;
        }
    }

    const CacheConfig& getConfig() const {
        return config;
    }

    int setCount() const {
        return numSets;
    }

    const Stats& getStats() const {
        return stats;
    }
};

SimulationResult simulateL1L2(const vector<Access>& trace,
                              Policy policy,
                              const CacheConfig& l1Config,
                              const CacheConfig& l2Config) {
    CacheLevel l1(l1Config);
    CacheLevel l2(l2Config);
    unordered_map<ull, vector<int>> futureL1;
    unordered_map<ull, vector<int>> futureL2;

    if (policy == BELADY) {
        futureL1 = l1.buildFuture(trace);
        futureL2 = l2.buildFuture(trace);
    }

    long long timer = 0;
    long long memoryAccesses = 0;

    for (int i = 0; i < (int)trace.size(); i++) {
        timer++;
        const Access& access = trace[i];

        if (l1.access(access, policy, timer))
            continue;

        if (l2.access(access, policy, timer)) {
            l1.insert(access, policy, timer, futureL1, i);
            continue;
        }

        memoryAccesses++;
        l2.insert(access, policy, timer, futureL2, i);
        l1.insert(access, policy, timer, futureL1, i);
    }

    SimulationResult result;
    result.policy = policy;
    result.l1Config = l1Config;
    result.l2Config = l2Config;
    result.l1 = l1.getStats();
    result.l2 = l2.getStats();
    result.traceAccesses = trace.size();
    result.memoryAccesses = memoryAccesses;
    return result;
}

void writeStatsLine(ostream& out, const string& name, const Stats& stats, long long traceAccesses) {
    double globalHitRate = traceAccesses ? (double)stats.hits * 100.0 / traceAccesses : 0.0;

    out << name << " Stats\n";
    out << "  Accesses: " << stats.accesses << '\n';
    out << "  Loads: " << stats.loads << '\n';
    out << "  Stores: " << stats.stores << '\n';
    out << "  Hits: " << stats.hits << '\n';
    out << "  Misses: " << stats.misses << '\n';
    out << "  Evictions: " << stats.evictions << '\n';
    out << fixed << setprecision(2);
    out << "  Local Hit Rate: " << stats.hitRate() << "%\n";
    out << "  Local Miss Rate: " << stats.missRate() << "%\n";
    out << "  Global Hit Rate: " << globalHitRate << "%\n\n";
}

void writeSimulationResult(ostream& out, const SimulationResult& result) {
    out << "Policy: " << policyName(result.policy) << '\n';
    out << "========================================\n";
    out << "Trace Accesses: " << result.traceAccesses << '\n';
    out << "Total Cache Hits: " << result.totalCacheHits() << '\n';
    out << "Memory Accesses after L2 Miss: " << result.memoryAccesses << '\n';
    out << fixed << setprecision(2);
    out << "Overall Hit Rate: " << result.overallHitRate() << "%\n";
    out << "Memory Access Rate: " << result.memoryAccessRate() << "%\n\n";
    writeStatsLine(out, "L1", result.l1, result.traceAccesses);
    writeStatsLine(out, "L2", result.l2, result.traceAccesses);
    out << '\n';
}

void writeSummaryTable(ostream& out, const vector<SimulationResult>& results) {
    out << "Summary For All Replacement Policies\n";
    out << "====================================\n";
    out << left << setw(10) << "Policy"
        << right << setw(14) << "Overall HR"
        << setw(14) << "L1 HR"
        << setw(14) << "L2 HR"
        << setw(14) << "Memory\n";
    out << string(66, '-') << '\n';

    for (const SimulationResult& result : results) {
        out << left << setw(10) << policyName(result.policy)
            << right << fixed << setprecision(2)
            << setw(13) << result.overallHitRate() << "%"
            << setw(13) << result.l1.hitRate() << "%"
            << setw(13) << result.l2.hitRate() << "%"
            << setw(14) << result.memoryAccesses << '\n';
    }

    out << "\n\n";
}

void writeCapacityElbow(ostream& out,
                        ofstream& csv,
                        const vector<Access>& trace,
                        const CacheConfig& baseL1,
                        const CacheConfig& baseL2) {
    vector<int> sizesKb = {2, 4, 8, 16, 32};

    out << "Capacity Elbow Data: Miss Rate vs Cache Size (LRU)\n";
    out << "==================================================\n";
    out << left << setw(14) << "L1 Size KB"
        << right << setw(14) << "L1 Miss %"
        << setw(14) << "L1 Hit %"
        << setw(14) << "Overall HR"
        << setw(14) << "Memory %"
        << setw(14) << "Memory\n";
    out << string(84, '-') << '\n';

    csv << "cache_size_kb,l1_accesses,l1_hits,l1_misses,l1_hit_rate,l1_miss_rate,l2_accesses,l2_hits,l2_misses,l2_hit_rate,l2_miss_rate,overall_hit_rate,memory_accesses,memory_access_rate\n";

    for (int sizeKb : sizesKb) {
        CacheConfig l1 = baseL1;
        l1.cacheSizeBytes = sizeKb * 1024;
        SimulationResult result = simulateL1L2(trace, LRU, l1, baseL2);

        out << left << setw(14) << sizeKb
            << right << fixed << setprecision(2)
            << setw(13) << result.l1.missRate() << "%"
            << setw(13) << result.l1.hitRate() << "%"
            << setw(13) << result.overallHitRate() << "%"
            << setw(13) << result.memoryAccessRate() << "%"
            << setw(14) << result.memoryAccesses << '\n';

        csv << sizeKb << ','
            << result.l1.accesses << ','
            << result.l1.hits << ','
            << result.l1.misses << ','
            << fixed << setprecision(4) << result.l1.hitRate() << ','
            << result.l1.missRate() << ','
            << result.l2.accesses << ','
            << result.l2.hits << ','
            << result.l2.misses << ','
            << result.l2.hitRate() << ','
            << result.l2.missRate() << ','
            << result.overallHitRate() << ','
            << result.memoryAccesses << ','
            << result.memoryAccessRate() << '\n';
    }

    out << "\n\n";
}

void writeAssociativityElbow(ostream& out,
                             ofstream& csv,
                             const vector<Access>& trace,
                             const CacheConfig& baseL1,
                             const CacheConfig& baseL2) {
    vector<int> associativities = {1, 2, 4, 8};

    out << "Associativity Elbow Data: Hit Rate vs Associativity (LRU)\n";
    out << "=========================================================\n";
    out << left << setw(14) << "L1 Ways"
        << right << setw(14) << "L1 Hit %"
        << setw(14) << "L1 Miss %"
        << setw(14) << "Overall HR"
        << setw(14) << "Memory %"
        << setw(14) << "Memory\n";
    out << string(84, '-') << '\n';

    csv << "associativity,l1_accesses,l1_hits,l1_misses,l1_hit_rate,l1_miss_rate,l2_accesses,l2_hits,l2_misses,l2_hit_rate,l2_miss_rate,overall_hit_rate,memory_accesses,memory_access_rate\n";

    for (int ways : associativities) {
        CacheConfig l1 = baseL1;
        l1.associativity = ways;
        SimulationResult result = simulateL1L2(trace, LRU, l1, baseL2);

        out << left << setw(14) << ways
            << right << fixed << setprecision(2)
            << setw(13) << result.l1.hitRate() << "%"
            << setw(13) << result.l1.missRate() << "%"
            << setw(13) << result.overallHitRate() << "%"
            << setw(13) << result.memoryAccessRate() << "%"
            << setw(14) << result.memoryAccesses << '\n';

        csv << ways << ','
            << result.l1.accesses << ','
            << result.l1.hits << ','
            << result.l1.misses << ','
            << fixed << setprecision(4) << result.l1.hitRate() << ','
            << result.l1.missRate() << ','
            << result.l2.accesses << ','
            << result.l2.hits << ','
            << result.l2.misses << ','
            << result.l2.hitRate() << ','
            << result.l2.missRate() << ','
            << result.overallHitRate() << ','
            << result.memoryAccesses << ','
            << result.memoryAccessRate() << '\n';
    }

    out << "\n\n";
}

int main(int argc, char* argv[]) {
    srand((unsigned int)time(nullptr));

    string traceFile = "trace.out";
    string statsFile = "statistics.out";
    if (argc >= 2) traceFile = argv[1];
    if (argc >= 3) statsFile = argv[2];

    vector<Access> trace = readTrace(traceFile);
    if (trace.empty()) {
        cerr << "Error: no valid memory accesses found in " << traceFile << '\n';
        return 1;
    }

    CacheConfig l1 = {"L1", 32 * 1024, 64, 4};
    CacheConfig l2 = {"L2", 256 * 1024, 64, 8};

    ofstream out(statsFile);
    ofstream capacityCsv("capacity_elbow.csv");
    ofstream associativityCsv("associativity_elbow.csv");
    if (!out || !capacityCsv || !associativityCsv) {
        cerr << "Error: could not create output files\n";
        return 1;
    }

    vector<SimulationResult> policyResults;
    for (Policy policy : ALL_POLICIES) {
        policyResults.push_back(simulateL1L2(trace, policy, l1, l2));
    }

    out << "Two-Level Cache Simulator Statistics\n";
    out << "====================================\n";
    out << "Trace File: " << traceFile << '\n';
    out << "Trace Accesses: " << trace.size() << "\n\n";
    out << "Default L1: 32 KB, 64 B block, 4-way\n";
    out << "Default L2: 256 KB, 64 B block, 8-way\n\n";

    writeSummaryTable(out, policyResults);

    for (const SimulationResult& result : policyResults) {
        writeSimulationResult(out, result);
    }

    writeCapacityElbow(out, capacityCsv, trace, l1, l2);
    writeAssociativityElbow(out, associativityCsv, trace, l1, l2);

    out.close();
    capacityCsv.close();
    associativityCsv.close();

    writeSummaryTable(cout, policyResults);
    cout << "Full stats written to " << statsFile << '\n';
    cout << "Capacity graph data written to capacity_elbow.csv\n";
    cout << "Associativity graph data written to associativity_elbow.csv\n";

    return 0;
}