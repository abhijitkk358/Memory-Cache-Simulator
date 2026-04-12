// Trace Extractor using Intel PIN
#include "pin.H"
#include <iostream>
#include <unordered_map> 
#include <queue>         
#include <fstream>

using namespace std;

// Pack structure to avoid padding : reduces file size
#pragma pack(push, 1)

struct TraceEntry {
    char type;     // 'R' for read & 'W' for write
    UINT64 addr;   // Memory address accessed
    UINT64 ip;     // Instruction pointer
};
#pragma pack(pop)

// Buffer size for batching writes : reduces I/O overhead
const int BUFFER_SIZE = 10000;

// In-memory buffer to store trace entries before writing to file
TraceEntry traceBuffer[BUFFER_SIZE];
int bufferIndex = 0;

// Output files
ofstream TraceFile;    
ofstream AnalysisFile; 

// Address range of main executable
ADDRINT mainLow = 0, mainHigh = 0;

// Global counter for total memory accesses
UINT64 global_mem_accesses = 0;

// Frequency map → counts how many times each address is accessed
std::unordered_map<VOID*, UINT64> addressFrequency;

// Stores last access time of each address (for temporal distance)
std::unordered_map<VOID*, UINT64> last_access_time; 

// Stores total temporal distance sum per address
std::unordered_map<VOID*, UINT64> sumdist;

// Flush buffer contents to file   (batch write)
inline void FlushBuffer() {
    if (bufferIndex > 0) {
            TraceFile.write((char*)traceBuffer, bufferIndex * sizeof(TraceEntry));
        bufferIndex = 0;
    }
}

// Core function to record memory access
VOID RecordMemAccess(VOID *ip, VOID *addr, char type) {
    global_mem_accesses++;

    // Store entry in buffer
    traceBuffer[bufferIndex].type = type;
    traceBuffer[bufferIndex].addr = (UINT64)addr;
    traceBuffer[bufferIndex].ip = (UINT64)ip;
    bufferIndex++;

    // Flush if buffer is full
    if (bufferIndex >= BUFFER_SIZE) FlushBuffer();

    // If address was seen before  compute temporal reuse distance
    if (addressFrequency[addr] != 0) {
        UINT64 distance = global_mem_accesses - last_access_time[addr];
        sumdist[addr] += distance;
    }
    
    // Update frequency and last access timestamp
    addressFrequency[addr]++;
    last_access_time[addr] = global_mem_accesses;
}

// Wrapper for memory read
VOID recordmemread(VOID *ip, VOID *addr) { RecordMemAccess(ip, addr, 'R'); }

// Wrapper for memory write
VOID recordmemwrite(VOID *ip, VOID *addr) { RecordMemAccess(ip, addr, 'W'); }

// Instrument each instruction
VOID Instruction(INS ins, VOID *v) {
    ADDRINT addr = INS_Address(ins);

    // Filter: Only instrument main executable (avoid libraries)
    if (addr < mainLow || addr > mainHigh) return;

    // Insert callback for memory read
    if (INS_IsMemoryRead(ins))
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)recordmemread,
                       IARG_INST_PTR, IARG_MEMORYREAD_EA, IARG_END);

    // Insert callback for memory write
    if (INS_IsMemoryWrite(ins))
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)recordmemwrite,
                       IARG_INST_PTR, IARG_MEMORYWRITE_EA, IARG_END);
}

// Finalization function (runs after program ends)
VOID Fini(INT32 code, VOID *v) {
    FlushBuffer();          // Ensure all data is written
    TraceFile.close();

    AnalysisFile.setf(std::ios::showbase);

    // Max heap to find top frequent addresses
    std::priority_queue< std::pair<UINT64, VOID*> > maxHeap;

    // Push all address-frequency pairs into heap
    for (auto const& pair : addressFrequency)
        maxHeap.push({pair.second, pair.first}); 

    // Print top 5 busiest addresses
    AnalysisFile << " TOP 5 BUSIEST MEMORY ADDRESSES \n";
    for (int i = 1; i <= 5 && !maxHeap.empty(); i++) {
        pair<UINT64, VOID*> topElement = maxHeap.top();
        AnalysisFile << "Rank " << i << ": Address "
                     << topElement.second << " with "
                     << topElement.first << " accesses.\n";
        maxHeap.pop(); 
    }
    
    // Print average temporal reuse distance
    AnalysisFile << "--- Average Temporal Distance ---\n";
    for(auto it : addressFrequency) {
        if(it.second > 1) {
             UINT64 reuses = it.second - 1;
             AnalysisFile << "Avg distance for " << it.first
                          << " is " << sumdist[it.first] / reuses
                          << " accesses.\n";
        }
    }

    // Print total accesses
    AnalysisFile << "\nTotal Memory Accesses Profiled: "
                 << global_mem_accesses << "\n";

    AnalysisFile.close();
}

// Identify main executable address range
VOID Image(IMG img, VOID *v) {
    if (IMG_IsMainExecutable(img)) {
        mainLow = IMG_LowAddress(img);
        mainHigh = IMG_HighAddress(img);
    }
}

// Main function
int main(int argc, char * argv[]) {
    if (PIN_Init(argc, argv)) return -1;

    // Open output files
    TraceFile.open("pure_trace.bin", ios::out | ios::binary);
    AnalysisFile.open("analysis_stats.txt");

    // Register instrumentation functions
    IMG_AddInstrumentFunction(Image, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Start program under PIN
    PIN_StartProgram();

    return 0;
}
