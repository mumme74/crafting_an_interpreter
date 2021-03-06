#ifndef LOX_DEBUGGER_H
#define LOX_DEBUGGER_H

#include <stdio.h>
#include "module.h"

typedef struct ObjClosure ObjClosure;

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
  const char *condition;
  const char *commands;
  ObjClosure *evalCondition;
  bool enabled,
       silenceCmds;
} Breakpoint;

typedef struct Watchpoint {
  struct Watchpoint *next;
  const char *expr; // the expression to evaluate
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
// re-point debbugger io streams
void setDebuggerIOStreams(FILE *in, FILE *out, FILE *err);
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
// get breakpoint at index or NULL if none existing
Breakpoint *getBreakpointByIndex(int brkpntNr);
// get breakpoint index for breakpoint or -1
int getBreakpointIndex(Breakpoint *breakpoint);
// remove breakpoints from breakpoints
bool clearBreakpoint(Breakpoint *breakpoint);
// remove breakpoint at line in module
bool clearBreakpointAtLine(int line, Module *module);
// clear breakpoint by its index
bool clearBreakpointByIndex(int brkpntNr);
// set a new watchpoint
void setWatchpoint(Watchpoint *watchpoint);
// create and insert a new watchpoint looking at ident
void setWatchpointByExpr(const char *expr);
// removes watchpoint from watchpoints and frees watchpoint
bool clearWatchpoint(Watchpoint *watchpoint);
// removes watchpoint with expr
bool clearWatchPointByExpr(const char *expr);
// get the watchpoint matching expr
Watchpoint *getWatchpoint(const char *expr);
// set specific debugger commands, such as a debug breakpoint file.
// it borrows ownership
void setInitCommands(const char *debuggerCmds);
// run specific debugger commands, VM calls this one
void runInitCommands();

// let debugger continue, how far it goes depends on its state
// and current breakpoints
void resumeDebugger();

// on GC mark phase
void markDebuggerRoots(ObjFlags flags);

// when vm does next expression
void onNextTick();

extern Debugger debugger;


#endif // LOX_DEBUGGER_H