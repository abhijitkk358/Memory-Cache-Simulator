#include <bits/stdc++.h>
using namespace std;
// #define int long long

const int CACHE_SIZE = 64;
const int BLOCK_SIZE = 8;
const int ASSOCIATIVITY = 2;

enum Policy { LRU, FIFO, RANDOM, BELADY,  SRRIP, HAWKEYE, DRP };

struct Access {
    int pc;
int addr;
};
string policyNames[] = {"LRU","FIFO", "RANDOM", "BELADY", "SRRIP", "HAWKEYE", "DRP"};


struct Block {
    bool valid = false;
    int tag = 0;
      int blckaddress = 0;   
  int LastUsed = 0;
  int insertTime = 0;
    
    int rrpv = 2; 
 int inserting_pc = 0; 
    bool got_hit = false;
    
    bool iSstable = false; // NEW: For DRP
};

int numBlocks = CACHE_SIZE / BLOCK_SIZE;
    int numSets = numBlocks / ASSOCIATIVITY;

  int offsetBits = log2(BLOCK_SIZE);

int indexbits = log2(numSets);

vector<Access> readTrace(string filename) {
    ifstream file(filename);
    vector<Access> trace;
    string ip, op, addr;
    int size;

    while (file >> ip >> op >> addr >> size) {
        int pc = stoll(ip, nullptr, 16);
        int address = stoll(addr, nullptr, 16);
        trace.push_back({pc, address});
    }
    return trace;
}

//LRU se konsa evict krna uska idx
int getLRU(vector<Block>& set) {
    int victim = 0;
    for (int i = 1; i < set.size(); i++)
        if (set[i].LastUsed < set[victim].LastUsed) victim = i;
    return victim;
}

int getFIFO(vector<Block>& set) {
    //initially set ke pehle element ko hi victim maan le
    int victim = 0;
    for (int i = 1; i < set.size(); i++)
        if (set[i].insertTime < set[victim].insertTime) victim = i;
    return victim;
}

int getRandom(int ways) {
    // ek set me utne block honge jitne ways hai 
    return rand() % ways;
}

int getBelady(vector<Block>& set, unordered_map<int, queue<int>>& future) {
    int victim = -1, farthest = -1;
    for (int i = 0; i < set.size(); i++) {
        int blckaddress = set[i].blckaddress;
        if (future[blckaddress].empty()) return i;
        
        int next_use = future[blckaddress].front();
        if (next_use > farthest) {
            farthest = next_use;
            victim = i;
        }
    }
    return victim;
}

// SRRIP : Static Re-Reference Interval Prediction
int getSRRIP(vector<Block>& set) {
    while (true) {
        for (int i = 0; i < set.size(); i++) {
            if (set[i].rrpv == 3) return i;
        }
        for (int i = 0; i < set.size(); i++) {
            if (set[i].rrpv < 3) set[i].rrpv++;
        }
    }
}


int getDRP(vector<Block>& set) {
    int oldestUnstable = -1;
    int OldestStble = -1;

        // saaare mese oldest maintain krte chl rhe
    for (int j = 0; j < set.size(); j++) {
        if (!set[j].iSstable) {
            if (oldestUnstable == -1 || set[j].LastUsed < set[oldestUnstable].LastUsed)
                oldestUnstable = j;
        } else {
            if (OldestStble == -1 || set[j].LastUsed < set[OldestStble].LastUsed)
                OldestStble = j;
        }
    }
    //agr unstable hai to oldest unstable return kro nhi to oldest stable
    if (oldestUnstable != -1) return oldestUnstable;
    return OldestStble; // Only evict stable if forced to
}

void simulate(vector<Access> trace, Policy policy, ofstream& out) {
    vector<vector<Block>> cache(numSets, vector<Block>(ASSOCIATIVITY));
    int hits = 0, misses = 0, evictions = 0;
    int timer = 0;
    //belady ke liye future queue
    unordered_map<int, queue<int>> future; //har ek addr ke liye queue

 unordered_map<int, int> hawkeye_pred; //pc -> score
    
    deque<int> victimbuffer; //for DRP
    int max_regret_size = 4; //same

    if (policy == BELADY) {
        for (int i = 0; i < trace.size(); i++) {
            // ye addr kb kb aaya pure trace me vo store kr rhe
            future[trace[i].addr >> offsetBits].push(i);
        }
    }

    for (int i = 0; i < trace.size(); i++) {
        int pc = trace[i].pc;

    int addr = trace[i].addr;  

        //address mese set index ,tag , nikalo
        int blckaddress = addr >> offsetBits;   
    int setIndex = blckaddress & ((1 << indexbits) - 1);   // modular lekr
        int tag = blckaddress >> indexbits;

        auto& set = cache[setIndex];
    timer++;

        if (policy == BELADY) future[blckaddress].pop(); //purane hata diye
    bool hit = false;

        //check cache me he ya nhi
        for (auto& block : set) {
            
            if (block.valid && block.tag == tag) {
                hits++;
                hit = true;
                if (policy == LRU || policy == HAWKEYE || policy == DRP) block.LastUsed = timer;
            if (policy == SRRIP) block.rrpv = 0; 

                if (policy == HAWKEYE) block.got_hit = true; 
                break;
            }
        }

            //agr hit nhi hua to
        if (!hit) {
            misses++;
            int tidx = -1;
            
            //khali jgh dhundho
            for (int j = 0; j < set.size(); j++) {
                if (!set[j].valid) {
                    tidx = j;
                    break;
                }
            }
            //agr khali jgh nhi milti -> capacity miss
            if (tidx == -1) {
                //kisiko evict krna pdega
                evictions++;
                //eveiction policy pr depend krega
                // get wale jitne bhi function he vo idx denge uss set me konsa evict krna 
                if (policy == LRU || policy == HAWKEYE) tidx = getLRU(set);
                else if (policy == FIFO) tidx = getFIFO(set);
              else if (policy == RANDOM) tidx = getRandom(ASSOCIATIVITY);
                 else if (policy == SRRIP) tidx = getSRRIP(set);
               else if (policy == DRP) {
                    tidx = getDRP(set);
                    
                    victimbuffer.push_back(set[tidx].blckaddress);
                    if (victimbuffer.size() > max_regret_size) victimbuffer.pop_front();
                }
                else tidx = getBelady(set, future);

                if (policy == HAWKEYE) {
                    int old_pc = set[tidx].inserting_pc;
                    if (set[tidx].got_hit) {
                        hawkeye_pred[old_pc] = min(7, hawkeye_pred[old_pc] + 1);
                    } else {
                        hawkeye_pred[old_pc] = max(0, hawkeye_pred[old_pc] - 1);
                    }
                }
            }

            set[tidx].valid = true;

         set[tidx].tag = tag;
            set[tidx].blckaddress = blckaddress; 
          set[tidx].insertTime = timer;

            if (policy == SRRIP) {
                set[tidx].rrpv = 2; 
            } 
            else if (policy == HAWKEYE) {
                set[tidx].inserting_pc = pc;
                set[tidx].got_hit = false;
                
                if (hawkeye_pred.find(pc) == hawkeye_pred.end()) {
                    hawkeye_pred[pc] = 4;
                }

                if (hawkeye_pred[pc] >= 4) {
                    set[tidx].LastUsed = timer; 
                } else {
                    set[tidx].LastUsed = -1;    
                }
            }
            else if (policy == DRP) {
                set[tidx].LastUsed = timer; 
                
                // Check if we regret evicting this block recently
                bool found = false;
                
                for (int r_addr : victimbuffer) {
                    if (r_addr == blckaddress) { found = true; break; }
                }

                set[tidx].iSstable = found;
            }
            else {
                set[tidx].LastUsed = timer; 
            }
        }
    }

    double hitRate = (double)hits / trace.size() * 100.0;
    
    cout << "Policy: " << policyNames[policy] << endl;
    cout << "Hits: " << hits << " Misses: " << misses << " Evictions: " << evictions << endl;

cout << "Hit Rate: " << hitRate << "%\n\n";

    out << "Policy: " << policyNames[policy] << endl;

  out << "Hits: " << hits << " Misses: " << misses << " Evictions: " << evictions << endl;
    out << "Hit Rate: " << hitRate << "%\n\n";
}

int main() {
    srand(time(0));
    vector<Access> trace = readTrace("trace6.out");
    ofstream out("statistics.out");

    simulate(trace, LRU, out);
    simulate(trace, FIFO, out);

  simulate(trace, RANDOM, out);
    simulate(trace, BELADY, out);

 simulate(trace, SRRIP, out);     
    simulate(trace, HAWKEYE, out);   
    simulate(trace, DRP, out);       

    out.close();
    
    cout << "Check statistics.out\n";
    return 0;
}