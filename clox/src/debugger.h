#ifndef LOX_DEBUGGER_H
#define LOX_DEBUGGER_H

#include "module.h"

typedef enum {
  // run continiously, ignore breakpoints
  DBG_RUN,
  // run until next breakpoint
  DBG_ARMED,
  // single step
  DBG_STEP,
  // step out of current function
  DBG_STEP_OUT,
  // single step but dont go into other functions
  DBG_NEXT,
  // halt debugger on next tick
  DBG_HALT,
  // stop current run, requires a restart after this
  DBG_STOP
} DebugStates;

typedef struct Breakpoint {
  Module *module;
  struct Breakpoint *next;
  int line, ignoreCount, hits;
} Breakpoint;

typedef struct Watchpoint {
  struct Watchpoint *next;
  const char *ident; // name of variable
} Watchpoint;

typedef struct Debugger {
  Breakpoint *breakpoints;
  Watchpoint *watchpoints;
  DebugStates state;
  bool isHalted;
} Debugger;

// initialize breakpoint
void initBreakpoint(Breakpoint *breakpoint);
// initialize watchpoint
void initWatchpoint(Watchpoint *watchpoint);
// free breakpoint
void freeBreakpoint(Breakpoint *breakpoint);
// free watchpoint
void freeWatchpoint(Watchpoint *watchpoint);
// initialize debugger
void initDebugger();
// return the current debug state
DebugStates debuggerState();
// sets the debugger state
void setDebuggerState(DebugStates state);
// insert a new breakpoint, debugger takes ownership
void setBreakpoint(Breakpoint *breakpoint);
// create a new breakpoint at line in module and insert it
void setBreakpointAtLine(int line, Module *module);
// returns breakpoint at line in module or NULL
Breakpoint *getBreakpoint(int line, Module *module);
// remove breakpoints from breakpoints
bool clearBreakpoint(Breakpoint *breakpoint);
// remove breakpoint at line in module
bool clearBreakpointAtLine(int line, Module *module);
// set a new watchpoint
void setWatchpoint(Watchpoint *watchpoint);
// create and insert a new watchpoint looking at ident
void setWatchpointByIdent(const char *ident);
// removes watchpoint from watchpoints and frees watchpoint
bool clearWatchpoint(Watchpoint *watchpoint);
// removes watchpoint with name ident
bool clearWatchPointByIdent(const char *ident);
// get the watchpoint looking at ident
Watchpoint *getWatchpoint(const char *ident);

// let debugger continue, how far it goes depends on its state
// and current breakpoints
void resumeDebugger();

// on GC mark phase
void markDebugger();

// when vm does next expression
void onNextTick();


#endif // LOX_DEBUGGER_H