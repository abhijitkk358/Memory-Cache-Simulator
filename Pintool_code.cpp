
//Trace Extractor
#include "pin.H"
#include <iostream>
#include <unordered_map> 
#include <queue>         
#include <fstream>

using namespace std;

#pragma pack(push, 1)
struct TraceEntry {
    char type;     
    UINT64 addr;
    UINT64 ip;
};
#pragma pack(pop)

const int BUFFER_SIZE = 10000;
TraceEntry traceBuffer[BUFFER_SIZE];
int bufferIndex = 0;

ofstream TraceFile;    
ofstream AnalysisFile; 

ADDRINT mainLow = 0, mainHigh = 0;
UINT64 global_mem_accesses = 0;

std::unordered_map<VOID*, UINT64> addressFrequency;
std::unordered_map<VOID*, UINT64> last_access_time; 
std::unordered_map<VOID*, UINT64> sumdist;

inline void FlushBuffer() {
    if (bufferIndex > 0) {
        TraceFile.write((char*)traceBuffer, bufferIndex * sizeof(TraceEntry));
        bufferIndex = 0;
    }
}

VOID RecordMemAccess(VOID *ip, VOID *addr, char type) {
    global_mem_accesses++;
    traceBuffer[bufferIndex].type = type;
    traceBuffer[bufferIndex].addr = (UINT64)addr;
    traceBuffer[bufferIndex].ip = (UINT64)ip;
    bufferIndex++;

    if (bufferIndex >= BUFFER_SIZE) FlushBuffer();

    if (addressFrequency[addr] != 0) {
        UINT64 distance = global_mem_accesses - last_access_time[addr];
        sumdist[addr] += distance;
    }
    
    addressFrequency[addr]++;
    last_access_time[addr] = global_mem_accesses;
}

VOID recordmemread(VOID *ip, VOID *addr) { RecordMemAccess(ip, addr, 'R'); }
VOID recordmemwrite(VOID *ip, VOID *addr) { RecordMemAccess(ip, addr, 'W'); }

VOID Instruction(INS ins, VOID *v) {
    ADDRINT addr = INS_Address(ins);
    if (addr < mainLow || addr > mainHigh) return;
    if (INS_IsMemoryRead(ins)) INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)recordmemread, IARG_INST_PTR, IARG_MEMORYREAD_EA, IARG_END);
    if (INS_IsMemoryWrite(ins)) INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)recordmemwrite, IARG_INST_PTR, IARG_MEMORYWRITE_EA, IARG_END);
}

VOID Fini(INT32 code, VOID *v) {
    FlushBuffer();
    TraceFile.close();

    AnalysisFile.setf(std::ios::showbase);
    std::priority_queue< std::pair<UINT64, VOID*> > maxHeap;
    for (auto const& pair : addressFrequency) maxHeap.push({pair.second, pair.first}); 

    AnalysisFile << "--- TOP 5 BUSIEST MEMORY ADDRESSES ---\n";
    for (int i = 1; i <= 5 && !maxHeap.empty(); i++) {
        pair<UINT64, VOID*> topElement = maxHeap.top();
        AnalysisFile << "Rank " << i << ": Address " << topElement.second << " with " << topElement.first << " accesses.\n";
        maxHeap.pop(); 
    }
    
    AnalysisFile << "--- Average Temporal Distance ---\n";
    for(auto it : addressFrequency) {
        if(it.second > 1) {
             UINT64 reuses = it.second - 1;
             AnalysisFile << "Avg distance for " << it.first << " is " << sumdist[it.first] / reuses << " accesses.\n";
        }
    }
    AnalysisFile << "\nTotal Memory Accesses Profiled: " << global_mem_accesses << "\n";
    AnalysisFile.close();
}

VOID Image(IMG img, VOID *v) {
    if (IMG_IsMainExecutable(img)) {
        mainLow = IMG_LowAddress(img);
        mainHigh = IMG_HighAddress(img);
    }
}

int main(int argc, char * argv[]) {
    if (PIN_Init(argc, argv)) return -1;
    TraceFile.open("pure_trace.bin", ios::out | ios::binary);
    AnalysisFile.open("analysis_stats.txt");
    IMG_AddInstrumentFunction(Image, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();
    return 0;
}
