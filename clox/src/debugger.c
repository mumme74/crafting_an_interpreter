#include <stdlib.h>
#include <stdio.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "vm.h"
#include "debugger.h"
#include "module.h"
#include "memory.h"
#include "chunk.h"
#include "scanner.h"

// -----------------------------------------------------

static Debugger dbg;
static CallFrame *frame = NULL;
static int line = 0;

static char *command = NULL;

static void printSource(int baseline, int window) {
  if (baseline < 1) baseline = line;

  int fromLine = baseline -window < 0 ? 0 : baseline -window,
      toLine   = baseline +window;
  int lineCnt  = 1;

  const char *src = vm.currentModule->source;
  putc('\n', stdout);

  while (*src != '\0') {
    if (*src == '\n') ++lineCnt;
    if (lineCnt >= fromLine && lineCnt <= toLine) {
      if (*src == '\n' || src == vm.currentModule->source) {
        printf("\n%-4d%s ", lineCnt, (lineCnt == line ? "*": " "));
        if (src == vm.currentModule->source) putc(*src, stdout);
      } else
        putc(*src, stdout);
    } if (lineCnt > toLine) break;
    src++;
  }
  putc('\n', stdout);
}

static void printIdent(const char *ident) {
  printf("printing variable: %s\n", ident);
}

static void skipWhitespace() {
  while (*command <= ' ' && *command != '\0')
    ++command;
}

static int readInt() {
  const char *start = command;
  while (*command >= '0' && *command <= '9')
    ++command;
  const char c = *command;
  *command = '\0';
  int i = atoi(start);
  *command = c;
  return i;
}

static const char *readWord() {
  static char buf[100] = {0}, *pbuf = buf;
  if (isalpha(*command) || (*command == '_'))
    *pbuf++ = (char)tolower(*command++);

  while (isalpha(*command) || isdigit(*command) ||
         (*command == '_') || *command == '.')
  {
    *pbuf++ = (char)tolower(*command++);
  }

  return buf;
}

static void parseCommand(char *cmd) {
  command = cmd;
  skipWhitespace();
  switch (*command++) {
  case 'c':
    dbg.state = DBG_ARMED;
    dbg.isHalted = false;
    return;
  case 's':
    dbg.state = DBG_STEP;
    dbg.isHalted = false;
    return;
  case 'n':
    dbg.state = DBG_NEXT;
    dbg.isHalted = false;
    return;
  case 'o':
    dbg.state = DBG_STEP_OUT;
    dbg.isHalted = false;
    return;
  case 'r':
    dbg.state = DBG_RUN;
    dbg.isHalted = false;
    return;
  case 'q': exit(0);
  case 'b':
    skipWhitespace();
    setBreakpointAtLine(readInt(), vm.currentModule);
    return;
  case 'w':
    skipWhitespace();
    const char *wd = readWord();
    setWatchpointByIdent(wd);
    return;
  case 'p':
    skipWhitespace();
    const char *ident = readWord();
    printIdent(ident);
    return;
  case 'l':
    skipWhitespace();
    printSource(readInt(), 5);
    return;
  default: printf("Unrecognized cmmand:%s", command);
  }
}

static void printWatchpoint(Watchpoint *wp) {
  printIdent(wp->ident);
}

static void printWatchpoints() {
  Watchpoint *wp = dbg.watchpoints;
  while (wp != NULL) {
    if (frame->closure == (ObjClosure*)wp->ident) {
      printWatchpoint(wp);
    }
    wp = wp->next;
  }
}

static void processEvents() {

  printWatchpoints();

  while (dbg.isHalted) {
    printf("**** debugger interface ****\n");
    char *buffer = readline("> ");
    if (buffer != NULL) {
      if (strlen(buffer) > 0) {
        add_history(buffer);
        parseCommand(buffer);
      }

      free(buffer);
    }
  }
}

static void checkBreakpoints() {
  frame = &vm.frames[vm.frameCount -1];
  line = frame->closure->function->chunk.lines[
    (int)(frame->ip - frame->closure->function->chunk.code)
  ];

  Breakpoint *bp = dbg.breakpoints;
  int cnt = 1;
  while (bp != NULL) {
    if (bp->module == vm.currentModule &&
        bp->line == line)
    {
      if (bp->hits++ >= bp->ignoreCount) {
        dbg.isHalted = true;
        printf("\n* stopped at breakpoint %d in %s\n* file:%s\n",
               ++cnt, vm.currentModule->name->chars,
               vm.currentModule->path->chars);
        printSource(line, 2);
        processEvents();
      }
    }
    bp = bp->next;
  }
}

// -----------------------------------------------------

void initBreakpoint(Breakpoint *breakpoint) {
  breakpoint->line =
    breakpoint->ignoreCount =
      breakpoint->hits = 0;

  breakpoint->module = NULL;
  breakpoint->next = NULL;
}

void initWatchpoint(Watchpoint *watchpoint) {
  watchpoint->ident = NULL;
  watchpoint->next = NULL;
}

void freeBreakpoint(Breakpoint *breakpoint) {
  FREE(Breakpoint, breakpoint);
}

void freeWatchpoint(Watchpoint *watchpoint) {
  FREE_ARRAY(char, (char*)watchpoint->ident,
             strlen(watchpoint->ident));
}

void initDebugger() {
  dbg.breakpoints = NULL;
  dbg.watchpoints = NULL;
  dbg.state = DBG_RUN;
  dbg.isHalted = false;
}

DebugStates debuggerState() {
  return dbg.state;
}

void setDebuggerState(DebugStates state) {
  dbg.state = state;
}

void setBreakpoint(Breakpoint *breakpoint) {
  Breakpoint *bp = dbg.breakpoints,
             **prev = &dbg.breakpoints;
  while(bp != NULL) {
    if (bp->module == breakpoint->module) {
      if (bp->line == breakpoint->line) {
        // replace breakpoint
        (*prev) = breakpoint;
        breakpoint->next = bp->next;
        freeBreakpoint(bp);
        return;
      } else if (bp->line > breakpoint->line) {
        // insert breakpoint
        (*prev) = breakpoint;
        breakpoint->next = bp;
        return;
      }
    }
    prev = &bp->next;
    bp = bp->next;
  }
  // not found, insert last
  *prev = breakpoint;
}

void setBreakpointAtLine(int line, Module *module) {
  Breakpoint *bp = ALLOCATE(Breakpoint, 1);
  initBreakpoint(bp);
  bp->line = line;
  bp->module = module;
  setBreakpoint(bp);
}

Breakpoint *getBreakpoint(int line, Module *module) {
  Breakpoint *bp = dbg.breakpoints;
  while (bp != NULL) {
    if (bp->module == module && bp->line == line)
      return bp;
  }
  return NULL;
}

bool clearBreakpoint(Breakpoint *breakpoint) {
  Breakpoint *bp = dbg.breakpoints,
             **prev = &dbg.breakpoints;
  while(bp != NULL) {
    if (bp->module == breakpoint->module &&
        bp->line == breakpoint->line)
    {
      (*prev) = bp->next;
      freeBreakpoint(bp);
      return true;
    }
    prev = &bp->next;
    bp = bp->next;
  }
  return false;
}

bool clearBreakpointAtLine(int line, Module *module) {
  Breakpoint *bp = getBreakpoint(line, module);
  if (bp) return clearBreakpoint(bp);
  return false;
}

void setWatchpoint(Watchpoint *watchpoint) {
  Watchpoint *wp = dbg.watchpoints,
            **prev = &dbg.watchpoints;
  while (wp != NULL) {
    if (strcmp(wp->ident, watchpoint->ident) == 0) {
      // replace it
      (*prev) =  watchpoint;
      watchpoint->next = wp->next;
      freeWatchpoint(wp);
      return;
    }
    prev = &wp->next;
    wp = wp->next;
  }
  // not found insert last
  *prev = watchpoint;
}

void setWatchpointByIdent(const char *ident) {
  Watchpoint *wp = ALLOCATE(Watchpoint, 1);
  initWatchpoint(wp);
  wp->ident = ALLOCATE(const char, strlen(ident)+1);
  strcpy((char*)wp->ident, ident);
}

bool clearWatchpoint(Watchpoint *watchpoint) {
  Watchpoint *wp = dbg.watchpoints,
            **prev = &dbg.watchpoints;
  while (wp != NULL) {
    if (wp == watchpoint) {
      // replace it
      (*prev) = wp->next;
      freeWatchpoint(wp);
      return true;
    }
    prev = &wp->next;
    wp = wp->next;
  }
  return false;
}

// removes watchpoint at module in closure with name ident
bool clearWatchPointByIdent(const char *ident) {
  Watchpoint *wp = getWatchpoint(ident);
  if (wp != NULL)
    return clearWatchpoint(wp);
  return false;
}

// get the watchpoint looking at ident in module and closure
Watchpoint *getWatchpoint(const char *ident) {
  Watchpoint *wp = dbg.watchpoints;
  while (wp != NULL) {
    if (strcmp(wp->ident, ident) == 0) {
      return wp;
    }
  }
  return NULL;
}

void resumeDebugger() {
  dbg.isHalted = false;
}

void markDebugger() {
  // not sure if it's needed?
}

void onNextTick() {
  switch (dbg.state) {
  case DBG_RUN: return;
  case DBG_NEXT:
  case DBG_STEP_OUT:
  case DBG_ARMED:
    return checkBreakpoints();
  case DBG_STEP: // fall through
  case DBG_HALT:
    dbg.isHalted = true;
    return processEvents();
  case DBG_STOP: break;
  }
}
