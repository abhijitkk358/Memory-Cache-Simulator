#include "pin.H"
#include <iostream>
#include <fstream>

using std::cerr;
using std::endl;
using std::ofstream;
using std::string;


ofstream traceFile;
bool inMainExecutable = false;
UINT64 count = 0;
UINT64 MAX_INS = 15;   



KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "trace.out", "trace output file");



// sirf main wale lene hai
VOID ImageLoad(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img))
    {
        inMainExecutable = true;
        cerr << "Main executable: " << IMG_Name(img) << endl;
    }
}

VOID RecordMemRead(VOID* ip, VOID* addr, UINT32 size)
{
    if (!inMainExecutable) return;
    if (count >= MAX_INS) return;

    traceFile << std::hex << ip << " L " << addr << " " << std::dec << size << endl;
    count++;
}

// Memory write
VOID RecordMemWrite(VOID* ip, VOID* addr, UINT32 size)
{
    if (!inMainExecutable) return;
    if (count >= MAX_INS) return;

    traceFile << std::hex << ip << " S " << addr << " " << std::dec << size << endl;
    count++;
}


VOID Instruction(INS ins, VOID* v)
{
    // removing stack noise
    if (INS_IsStackRead(ins) || INS_IsStackWrite(ins))
        return;

    // memory read
    if (INS_IsMemoryRead(ins))
    {
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE,
            (AFUNPTR)RecordMemRead,
            IARG_INST_PTR,
            IARG_MEMORYREAD_EA,
            IARG_MEMORYREAD_SIZE,
            IARG_END);
    }

    // Memory write
    if (INS_IsMemoryWrite(ins))
    {
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE,
            (AFUNPTR)RecordMemWrite,
            IARG_INST_PTR,
            IARG_MEMORYWRITE_EA,
            IARG_MEMORYWRITE_SIZE,
            IARG_END);
    }
}


VOID Fini(INT32 code, VOID* v)
{
    traceFile.close();
    cerr << "Total traced memory accesses: " << count << endl;
}


int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv))
    {
        cerr << "PIN Init failed" << endl;
        return 1;
    }

    traceFile.open(KnobOutputFile.Value().c_str());

    IMG_AddInstrumentFunction(ImageLoad, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    cerr << "==========================================" << endl;
    cerr << "Trace generation started" << endl;
    cerr << "Max entries: " << MAX_INS << endl;
    cerr << "==========================================" << endl;

    PIN_StartProgram();

    return 0;
}
