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
static CallFrame *frame = NULL,
                 *breakFrame = NULL;
static int line = 0;

static void parseCommand(char *buffer);


// prints the source around baseline,
// window = how many rows before and after
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

// print identifier value
static void printIdent(const char *ident) {
  printf("printing variable: %s\n", ident);
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
  static char *prev = NULL;

  rl_completer_word_break_characters = " .";
  while (dbg.isHalted) {
    printf("**** debugger interface ****\n");
    char *buffer = readline("> ");
    if (buffer != NULL) {
      if (strlen(buffer) > 0) {
        add_history(buffer);
        free(prev);
        prev = buffer;
      } else {
        // remember cmd
        free(buffer);
        buffer = prev;
      }
      parseCommand(buffer);
    }
  }
}

static void checkNext() {
  static const OpCode haltOn[] = {
    OP_DEFINE_GLOBAL, OP_SET_GLOBAL, OP_SET_LOCAL,
    OP_SET_UPVALUE, OP_SET_PROPERTY, OP_EQUAL,
    OP_LESS, OP_ADD, OP_SUBTRACT, OP_DIVIDE,
    OP_PRINT, OP_JUMP, OP_JUMP_IF_FALSE, OP_LOOP,
    OP_INVOKE, OP_SUPER_INVOKE, OP_CLOSE_UPVALUE,
    OP_RETURN, OP_CLASS, OP_METHOD, OP_DICT,
    OP_DICT_FIELD
  };


  frame = &vm.frames[vm.frameCount -1];

  for (int i = 0; i < sizeof(haltOn) / sizeof(haltOn[0]); ++i) {
    if (haltOn[i] == *frame->ip) {
      line = frame->closure->function->chunk.lines[
        (int)(frame->ip - frame->closure->function->chunk.code)];
      dbg.isHalted = true;
      printSource(line, 2);
      processEvents();
    }
  }
}

static void checkBreakpoints() {
  frame = &vm.frames[vm.frameCount -1];
  line = frame->closure->function->chunk.lines[
    (int)(frame->ip - frame->closure->function->chunk.code)];

  Breakpoint *bp = dbg.breakpoints;
  int cnt = 1;
  while (bp != NULL) {
    if (bp->module == vm.currentModule &&
        bp->line == line)
    {
      if (!bp->enabled) return;
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

// -----------------------------------------------------------------
// parse commands, subset of gdb commands

/*
subset of
GDB command interface grammar
https://visualgdb.com/gdbreference/commands/
// begin commands
backtrace  = "backtrace"
           | "bt" ;

break      = "break" (file ":")? line
           | "break"                  (* set breakpoint at current line*)
           | "b" (file ":")? line ;

clear      = "clear" (file ":")? line
           | "clear" ;                (* delete all breakpoints *)

condition  = "cond" nr expression ;

continue   = "continue"
           | "c" ;

delete     = "delete" nr
           | "del" nr ;

disable    = "disable" nr
           | "dis" nr ;

down       = "down" nr ;

enable     = "enable" nr
           | "en" nr ;

frame      = "frame" nr?

help       = "help" hlp_cmds
           | "help" ;


ignore     = "ignore" nr count ;

info       = "info" nfo_brk
           | "info" nfo_wtch
           | "info" nfo_frm
           | "info" nfo_loc
           | "info" nfo_gbl ;

next       = "next"
           | "n" ;

print      = "print" expression
           | "p" expression

quit       = "quit" ;

step       = "step"
           | "s" ;

up         = "up" nr ;

watch      = "watch" expression ;

// end commands

nfo_brk    = "break" ;
nfo_wtch   = "watch" ;
nfo_frm    = "frame" ;
nfo_loc    = "locals" ;
nfo_gbl    = "globals" ;

hlp_cmds   = "backtrace" | "bt"
           | "break"     |  "b"
           | "clear"
           | "cond"
           | "continue"  | "c"
           | "delete"    |  "del"
           | "disable"   |  "dis"
           | "down"
           | "enable"    |  "en"
           | "frame"
           | "info"
           | "ignore"
           | "next"      | "n"
           | "print"     | "p"
           | "quit"
           | "step"      | "s"
           | "up"
           | "watch" ;

// epsilons
file       = ([/\\]?[^\/]*[/\\])*[\n]*\.lox ;
line       = [0-9]+ ;
nr         = [0-9]+ ;
count      = [0-9]+ ;
expression = [^\n]* ;
*/

/*
typedef enum DbgTokenType {
  DBG_TOK_BACKTRACE, DBG_TOK_BREAK, DBG_TOK_CLEAR,
  DBG_TOK_CONDITION, DBG_TOK_CONTINUE,
  DBG_TOK_DELETE, DBG_TOK_DISABLE,
  DBG_TOK_DOWN, DBG_TOK_ENABLE, DBG_TOK_FRAME,
  DBG_TOK_HELP, DBG_TOK_IGNORE, DBG_TOK_INFO,
  DBG_TOK_PRINT, DBG_TOK_QUIT, DBG_TOK_STEP,
  DBG_TOK_UP, DBG_TOK_WATCH,
  DBG_TOK_LOCALS, DBG_TOK_GLOBALS,
  DBG_TOK_LITERAL, DBG_NUMBER
} DbgTokenType;

typedef struct DbgToken {
  const char *len;
  int len;
  DbgTokenType type;
  DbgToken *next;
} DbgToken;

*/

typedef struct CmdTbl {
  const char *name;
  int nameLen;
  void (*parseFn)();
} CmdTbl;

// command string, moves when we parse
static char *cmd = NULL;

// ignore whitespace
static void skipWhitespace() {
  while (*cmd <= ' ' && *cmd != '\0')
    ++cmd;
}

// read integer
static int readInt() {
  const char *start = cmd;
  while (*cmd >= '0' && *cmd <= '9')
    ++cmd;
  const char c = *cmd;
  *cmd = '\0';
  int i = atoi(start);
  *cmd = c;
  return i;
}

// read a word, such as a identifier
static const char *readWord() {
  static char buf[100] = {0}, *pbuf = buf;
  if (isalpha(*cmd) || (*cmd == '_'))
    *pbuf++ = (char)tolower(*cmd++);

  while (isalpha(*cmd) || isdigit(*cmd) ||
         (*cmd == '_'))
  {
    *pbuf++ = (char)tolower(*cmd++);
  }
  *pbuf = '\0';

  return buf;
}

static const char *readPath() {
  skipWhitespace();
  static char buf[1024] = {0}, *pbuf = buf;
  while (*cmd != ':' && *cmd != '\0')
    *pbuf++ = *cmd++;
  // trim whitespace at end
  while (isspace(*pbuf)) --pbuf;
  *pbuf = '\0';
  return buf;
}

static void nfo_brk() {
  printf("nfo_brk");
}

static void nfo_wtch() {
  printf("nfo_wtch");
}

static void nfo_frm() {
  printf("nfo_frm");
}

static void nfo_loc() {
  printf("nfo_loc");
}

static void nfo_gbl() {
  printf("nfo_gbl");
}

CmdTbl infoCmds[] = {
  { "break", 5, nfo_brk },
  { "watch", 5, nfo_wtch },
  { "frame", 5, nfo_frm },
  { "locals", 6, nfo_loc },
  { "globals", 7, nfo_gbl }
};

static void info_() {
  skipWhitespace();
  for (int i =  0; i < sizeof(infoCmds) / sizeof(infoCmds[0]); ++i) {
    if (memcmp(cmd, infoCmds[i].name, infoCmds[i].nameLen) == 0) {
      cmd += infoCmds[i].nameLen;
      infoCmds[i].parseFn();
      return;
    }
  }

  printf("Unrecognized info cmd %s\n", readWord());
}

typedef struct HlpInfo {
  const char *name;
  int nameLen;
  const char *msg;
} HlpInfo;

const HlpInfo hlpInfos[] = {
  {
    "backtrace", 9,
    "backtrace       Prints the stacktrace of current state.\n"
    "backtrace nr    Select backtrace frame.\n"
  },{
    "bt", 2,
    "bt              Shorthand for backtrace\n"
  },{
    "break", 5,
    "break           Sets a breakpoint at current line.\n"
    "break line      Sets a breakpoint at line in current file.\n"
    "break file:line Sets a breakpoint at line in file"
  },{
    "clear", 5,
    "clear           Clears all breakpoints.\n"
    "clear nr        Clears breakpoint with number nr\n"
  },{
    "cond", 4,
    "cond nr expression   Sets a condition that triggers breakpoint.\n"
  },{
    "continue", 8,
    "continue        Continues execution until next breakpoint triggers.\n"
  },{
    "c", 1,
    "c               Shorthand for continue.\n"
    "                Continues execution until next breakpoint triggers.\n"
  },{
    "delete", 6,
    "delete nr       Deletes breakpoint with nr.\n"
  },{
    "del", 3,
    "del nr          Shorthand for delete.\n"
    "                Deletes breakpoint with nr.\n"
  },{
    "disable", 7,
    "disable         Disables current breakpoint.\n"
    "disable nr      Disables breakpoint with nr.\n"
  },{
    "dis", 3,
    "dis             Shorthand for disable.\n"
  },{
    "down", 4,
    "down            Go down in backtrace.\n"
  },{
    "enable", 6,
    "enable          Enable current breakpoint.\n"
    "enable nr       Enable breakpoint with nr.\n"
  },{
    "en", 2,
    "en              Shorthand for enable\n"
    "en nr           Shorthand for enable nr\n"
  },{
    "frame", 5,
    "frame           Select current frame.\n"
    "frame nr        Select frame nr in backtrace.\n"
  },{
    "info", 4,
    "info break      Show breakpoints.\n"
    "info watch      Show watchpoints.\n"
    "info frame      Show selected frame.\n"
    "info locals     Show all locals in current frame.\n"
    "info globals    Show all globals.\n"
  },{
    "ignore", 6,
    "ignore nr hits  Ignore the first number of hits to breakpoint nr.\n"
  },{
    "next", 4,
    "next            Step forward one, step over function calls.\n"
  },{
    "n", 1,
    "n               Shorthand for next\n"
  },{
    "print", 5,
    "print expression    Prints result of expression, might be a variable.\n"
  },{
    "p", 1,
    "p expression        Shorthand for print.\n"
  },{
    "quit", 4,
    "quit            Exits debugger.\n"
  },{
    "step", 4,
    "step            Steps to next pos in code.\n"
  },{
    "s", 1,
    "s               Shorthand for step.\n"
  },{
    "up", 2,
    "up              Goes up a frame in backtrace.\n"
  },{
    "watch", 5,
    "watch  expression   A watchpoint that gets evaluated each stop.\n"
  }
};

static void help_() {
  skipWhitespace();
  if (*cmd == '\0') {
    for (int i = 0; i < sizeof(hlpInfos) / sizeof(hlpInfos[0]); ++i) {
      printf("\n%s", hlpInfos[i].msg);
    }
    return;
  }

  for (int i = 0; i < sizeof(hlpInfos) / sizeof(hlpInfos[0]); ++i) {
    if (memcmp(cmd, hlpInfos[i].name, hlpInfos[i].nameLen) == 0) {
      printf("%s", hlpInfos[i].msg);
      cmd += hlpInfos[i].nameLen;
      return;
    }
  }
  printf("Unrecognized command to help %s\n", readWord());
}

static void backtrace_() {
  // FIXME implement
  skipWhitespace();
  if (*cmd != '\0') {
    int nr = readInt();
    (void)nr;
  }
  printf("backtrace\n");
}

static void readLineAndPath(int *lnNr, const char **path) {
  skipWhitespace();
  *path = vm.currentModule->path->chars;
  *lnNr = line;
  if (*cmd != '\0') {
    if (isalpha(*cmd)) {
      *path = readPath();
      if (*cmd != ':') {
        printf("Expected ':' between file and linenr, but got: %c.\n", *cmd);
        return;
      }
    }
    if (isdigit(*cmd))
      *lnNr = readInt();
  }
}

static void break_() {
  int lnNr;
  const char *path;
  readLineAndPath(&lnNr, &path);
  Module *mod = getModule(path);
  if (!mod) {
    printf("Module with path:%s not loaded.\n", path);
    return;
  }
  setBreakpointAtLine(line, mod);
  printf("Set breakpoint at %s:%d\n", path, lnNr);
}

static void clear_() {
  int lnNr;
  const char *path;
  readLineAndPath(&lnNr, &path);
  Module *mod = getModule(path);
  if (!mod) {
    printf("Module with path:%s not loaded.\n", path);
    return;
  }
  if (clearBreakpointAtLine(line, mod)) {
    printf("Cleared breakpoint at %s:%d\n", path, lnNr);
  } else {
    printf("Breakpoint not found, %s:%d \n", path, line);
  }

}

static void cond_() {
  // FIXME implement
  printf("cond\n");
}

static void continue_() {
  breakFrame = NULL;
  dbg.state = DBG_ARMED;
  dbg.isHalted = false;
}

static void delete_() {
  skipWhitespace();
  if (*cmd == '\0' || !isdigit(*cmd)) {
    printf("Expects breakpoint nr after delete command.\n");
    return;
  }

  clearBreakpoint(getBreakpointByIndex(readInt()));
  printf("Delete breakpoint.");
}

static void setEnable(const char *cmd, bool state) {
  skipWhitespace();
  if (*cmd != '\0') {
    if (!isdigit(*cmd)) {
      printf("Expect breakpoint nr after %s command.\n", cmd);
    }
    int nr = readInt();
    Breakpoint *bp = getBreakpointByIndex(nr);
    if (!bp) {
      printf("Breakpoint %d not found.\n", nr);
      return;
    }
    bp->enabled = state;
    printf("Set breakpoint %sed.\n", cmd);
    return;
  }

  // change all
  Breakpoint *bp = dbg.breakpoints;
  while (bp != NULL) {
    bp->enabled = state;
    bp = bp->next;
  }
}

static void disable_() {
  setEnable("disable", false);
}

static void down_() {
  // FIXME implement
  printf("down\n");
}

static void enable_() {
  setEnable("enable", true);
}

static void frame_() {
  // FIXME implement
  skipWhitespace();
  if (*cmd != '\0') {
    if (!isdigit(*cmd)) {
      printf("Expect nr after frame\n");
      return;
    }
    int nr = readInt();
    printf("Select frame %d", nr);
    return;
  }
  printf("Select current frame");
}

static void ignore_() {
  skipWhitespace();
  if (*cmd != '\0' || !isdigit(*cmd)) {
    printf("Expect breakpoint nr after ignore cmd\n");
    return;
  }

  int nr = readInt();
  skipWhitespace();
  if (*cmd != '\0' || !isdigit(*cmd)) {
    printf("Expect ignore count after breakpoint nr.\n");
    return;
  }

  int hits = readInt();

  Breakpoint *bp = getBreakpointByIndex(nr);
  if (bp) {
    bp->ignoreCount = hits;
  }
}

static void next_() {
  breakFrame = frame;
  dbg.state = DBG_NEXT;
  dbg.isHalted = false;
}

static void print_() {
  // FIXME implement
  skipWhitespace();
  if (*cmd == '\0') {
    printf("Expect a expression as param to print.\n");
    return;
  }

  const char *expr = cmd;
  printf("printing  %s", expr);
}

static void quit_() {
  exit(0);
}

static void step_() {
  breakFrame = NULL;
  dbg.state = DBG_STEP;
  dbg.isHalted = false;
}

static void up_() {
  // FIXME implement
  printf("up\n");
}

static void watch_() {
  if (*cmd == '\0') {
    printf("Expect a expression as param to watch.\n");
    return;
  }

  printf("Setting watch %s\n", cmd);
  setWatchpointByIdent(cmd);
}

const CmdTbl cmds[] = {
  { "backtrace", 9, backtrace_ },
  { "bt", 2, backtrace_ },
  { "break", 5, break_  },
  { "clear", 5, clear_},
  { "cond", 4, cond_},
  { "continue", 8, continue_},
  { "c", 1, continue_},
  { "delete", 6, delete_},
  { "del", 3, delete_},
  { "disable", 7, disable_},
  { "dis", 3, disable_},
  { "down", 4, down_},
  { "enable", 6, enable_},
  { "en", 2, enable_},
  { "frame", 5, frame_},
  { "help", 4, help_ },
  { "info", 4, info_},
  { "ignore", 6, ignore_},
  { "next", 4, next_},
  { "n", 1, next_},
  { "print", 5, print_},
  { "p", 1, print_},
  { "quit", 4, quit_},
  { "step", 4, step_},
  { "s", 1, step_},
  { "up", 2, up_},
  { "watch", 5, watch_}
};

static void parseCommand(char *command) {
  if (!command) return;
  cmd = command;
  skipWhitespace();
  for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); ++i) {
    if (memcmp(cmd, cmds[i].name, cmds[i].nameLen) == 0) {
      cmd += cmds[i].nameLen;
      cmds[i].parseFn();
      return;
    }
  }

  printf("Unrecognized command:%s\n", command);

  /*switch (*cmd++) {
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
  default: printf("Unrecognized command:%s\n", command);
  } */
}

// end command parser
// -----------------------------------------------------

void initBreakpoint(Breakpoint *breakpoint) {
  breakpoint->line =
    breakpoint->ignoreCount =
      breakpoint->hits = 0;

  breakpoint->module = NULL;
  breakpoint->next = NULL;
  breakpoint->enabled = true;
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


// get breakpoint at index or NULL if none existing
Breakpoint *getBreakpointByIndex(int brkpntNr) {
  int nr = 0;
  Breakpoint *bp = dbg.breakpoints;
  while (bp != NULL) {
    if (brkpntNr == nr)
      return bp;
    ++nr;
    bp = bp->next;
  }
  return NULL;
}

int getBreakpointIndex(Breakpoint *breakpoint) {
  int idx = 0;
  Breakpoint *bp = dbg.breakpoints;
  while (bp != NULL) {
    if (bp == breakpoint)
      return idx;
    idx++;
    bp = bp->next;
  }
  return -1;
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

bool clearBreakpointByIndex(int brkpntNr) {
  Breakpoint *bp = getBreakpointByIndex(brkpntNr);
  if (bp)
    return clearBreakpoint(bp);
  return NULL;
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
  case DBG_STEP_OUT:
  case DBG_ARMED:
    return checkBreakpoints();
  case DBG_NEXT:
    return checkNext();
  case DBG_STEP: // fall through
  case DBG_HALT:
    dbg.isHalted = true;
    return processEvents();
  case DBG_STOP: break;
  }
}
