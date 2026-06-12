#include "scheduler.h"
#include "assembler.h"
#include <iostream>
#include <string>
#include <vector>

using namespace std;

static void usage() {
    cout << "Usage: cpu [options] prog0.asm [prog1.asm ...]\n"
         << "  --quantum N      instructions per timeslice (default 8)\n"
         << "  --timer N        timer interrupt interval (default 32)\n"
         << "  --no-trace       suppress per-instruction output\n"
         << "  --no-sched       suppress context-switch log\n";
}

int main(int argc, char* argv[])
{
    HartManager mgr;
    vector<string> files;

    for(int i=1;i<argc;i++){
        string a=argv[i];
        if(a=="--no-trace")                   mgr.cfg.trace=false;
        else if(a=="--no-sched")              mgr.cfg.schedTrace=false;
        else if(a=="--quantum"&&i+1<argc)     mgr.cfg.quantum=stoi(argv[++i]);
        else if(a=="--timer"&&i+1<argc)       mgr.cfg.timerInterval=stoi(argv[++i]);
        else if(a=="--help")                  { usage(); return 0; }
        else                                  files.push_back(a);
    }

    if(files.empty()){ cout<<"No input files.\n"; return 1; }

    for(auto& path : files){
        int id = (int)mgr.harts.size();
        uint32_t base = hartSlotBase(id);

        Assembler as;
        if(!as.loadFile(path, base) || !as.finalise()){
            cout<<"Assembly failed: "<<path<<"\n"; return 1;
        }
        as.dump(path);
        int hid = mgr.spawnHart(as.program);
        cout << "  Spawned hart" << hid
             << " at PC=0x" << hex << base << dec << "\n";
    }

    cout << "\n";
    mgr.run();
    return 0;
}