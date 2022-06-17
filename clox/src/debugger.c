#include <stdlib.h>
#include <stdio.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "vm.h"
#include "debugger.h"
#include "compiler.h"
#include "module.h"
#include "memory.h"
#include "chunk.h"
#include "scanner.h"

// exported
Debugger debugger;

// -----------------------------------------------------

static CallFrame *frame = NULL,
                 *breakFrame = NULL;
static int line = 0;

static void parseCommands(const char *buffer);
static const char *initCommands = NULL;

// prints the source around baseline,
// window = how many rows before and after
static void printSource(int baseline, int window) {
  if (baseline < 1) baseline = line;

  int fromLine = baseline -window < 0 ? 0 : baseline -window,
      toLine   = baseline +window;
  int lineCnt  = 1;

  Module *module = getCurrentModule();
  const char *src = module->source;
  putc('\n', stdout);

  while (*src != '\0') {
    if (*src == '\n') ++lineCnt;
    if (lineCnt >= fromLine && lineCnt <= toLine) {
      if (*src == '\n' || src == module->source) {
        printf("\n%-4d%s ", lineCnt, (lineCnt == line ? "*": " "));
        if (src == module->source) putc(*src, stdout);
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

static void printWatchpoints() {
  Watchpoint *wp = debugger.watchpoints;
  for (;wp != NULL; wp = wp->next) {
    Value value;
    if (vm_eval(&value, wp->expr) == INTERPRET_OK &&
        !IS_NIL(value))
    {
      printf(" %s:%s\n", wp->expr, valueToString(value)->chars);
    }
  }
}

static void processEvents() {

  printWatchpoints();
  static char *prev = NULL;

  rl_completer_word_break_characters = " .";
  while (debugger.isHalted) {
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
      parseCommands(buffer);
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
      debugger.isHalted = true;
      printSource(line, 0);
      processEvents();
    }
  }
}

static void checkStepOut() {
  frame = &vm.frames[vm.frameCount -1];
  if (*frame->ip == OP_RETURN) {
    line = frame->closure->function->chunk.lines[
      (int)(frame->ip - frame->closure->function->chunk.code)];
    debugger.isHalted = true;
    printSource(line, 0);
    processEvents();
  }
}

static void checkBreakpoints() {
  frame = &vm.frames[vm.frameCount -1];
  line = frame->closure->function->chunk.lines[
    (int)(frame->ip - frame->closure->function->chunk.code)];

  Module *module = getCurrentModule();
  Breakpoint *bp = debugger.breakpoints;
  for (int nr = 0; bp != NULL; ++nr, bp = bp->next) {
    if (bp->module == module &&
        bp->line == line)
    {
      if (!bp->enabled) return;
      if (bp->condition) {
        if (!bp->evalCondition) {
          if (vm_evalBuild(&bp->evalCondition, bp->condition) !=
              INTERPRET_OK) {
            printf("Breakpoint %d condition invalid.(%s)\n",
                  nr, bp->condition);
            FREE_ARRAY(char, (char*)bp->condition,
                      strlen(bp->condition) +1);
            bp->condition = NULL;
            goto afterEvalCondtion;
          }
        }

        Value vlu;
        if (vm_evalRun(&vlu, bp->evalCondition) == INTERPRET_OK &&
            isFalsey(vlu))
        {
          continue;;
        }
      }

afterEvalCondtion:
      if (bp->hits++ >= bp->ignoreCount) {
        debugger.isHalted = true;
        printf("\n* stopped at breakpoint %d in %s\n* file:%s\n",
               nr, module->name->chars,
               module->path->chars);
        printSource(line, 2);
        processEvents();
      }
    }
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

typedef struct CmdTbl {
  const char *name;
  int nameLen;
  void (*parseFn)();
} CmdTbl;

// command string, moves when we parse
static const char *cmd = NULL;

// checks if at end
static bool isAtEnd() {
  return *cmd == '\0' || *cmd == '\n';
}

// ignore whitespace
static void skipWhitespace() {
  while (!isAtEnd() && isspace(*cmd))
    ++cmd;
}

// read integer
static int readInt() {
  char buf[25];
  char *pbuf = buf;

  while (*cmd >= '0' && *cmd <= '9')
    *pbuf++ = *cmd++;
  *pbuf = '\0';
  int i = atoi(buf);
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

// reads a filepath
static const char *readPath() {
  skipWhitespace();
  static char buf[1024] = {0}, *pbuf = buf;
  while (*cmd != ':' && !isAtEnd())
    *pbuf++ = *cmd++;
  // trim whitespace at end
  while (isspace(*pbuf)) --pbuf;
  *pbuf = '\0';
  return buf;
}

// reads from current pos to end of row
// reciever takes ownership of row
static int readRestOfRow(char **row) {
  const char *start = cmd;
  while(!isAtEnd())
    ++cmd;

  *row = ALLOCATE(char, cmd - start + 1);
  memcpy(*row, start, cmd - start);
  (*row)[cmd-start] = '\0';
  return cmd - start;
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
    "cond nr              Clears condition for breakpoint nr.\n"
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
    "echo", 4,
    "echo  string    Prints string, might be multiline if escaped.\n"
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
    "finish", 6,
    "finish          Run until current function return.\n"
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
  if (isAtEnd()) {
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
  if (!isAtEnd()) {
    int nr = readInt();
    (void)nr;
  }
  printf("backtrace\n");
}

static void readLineAndPath(int *lnNr, const char **path) {
  skipWhitespace();
  *path = getCurrentModule()->path->chars;
  *lnNr = line;
  if (!isAtEnd()) {
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
  setBreakpointAtLine(lnNr, mod);
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
  if (clearBreakpointAtLine(lnNr, mod)) {
    printf("Cleared breakpoint at %s:%d\n", path, lnNr);
  } else {
    printf("Breakpoint not found, %s:%d \n", path, line);
  }

}

static void comment_() {
  while (!isAtEnd())
    ++cmd;
}

static void cond_() {
  skipWhitespace();
  if (isAtEnd() || !isdigit(*cmd)) {
    printf("Expect breakpoint nr after 'cond'.\n");
    return;
  }

  int nr = readInt();
  Breakpoint *bp = getBreakpointByIndex(nr);
  if (bp == NULL) {
    printf("Breakpoint %d not found.\n", nr);
    return;
  }

  // clear old condition
  if (bp->condition != NULL)
    FREE_ARRAY(char, (char*)bp->condition, strlen(bp->condition)+1);
  if (bp->evalCondition)
    bp->evalCondition = NULL;

  skipWhitespace();
  if (isAtEnd()) {
    // clear condition
    printf("Cleared condition for breakpoint %d at %s:%d.\n",
          nr, bp->module->path->chars, bp->line);
    return;
  }

  char *cond;
  readRestOfRow(&cond);

  if (bp->condition != NULL)
    FREE_ARRAY(char, (char*)bp->condition, strlen(bp->condition)+1);
  bp->condition = cond;

  printf("condtion %s set for breakpoint %d at %s:%d\n",
         cond, nr, bp->module->path->chars, bp->line);
}

static void continue_() {
  breakFrame = NULL;
  debugger.state = DBG_ARMED;
  debugger.isHalted = false;
}

static void delete_() {
  skipWhitespace();
  if (isAtEnd() || !isdigit(*cmd)) {
    printf("Expects breakpoint nr after delete command.\n");
    return;
  }

  clearBreakpoint(getBreakpointByIndex(readInt()));
  printf("Delete breakpoint.");
}

static void setEnable(const char *cmd, bool state) {
  skipWhitespace();
  if (isAtEnd()) {
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
  Breakpoint *bp = debugger.breakpoints;
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

static void echo_() {
  skipWhitespace();
  const char *start = cmd;
  while (*cmd != '\0') {
    if (*cmd != '\\' && *(cmd +1) == '\n')
      break;
    ++cmd;
  }

  while (start <= cmd) {
    if (*start == '\\' && *(++start) == 'n')
      putc('\n', stdout);
    else
      putc(*start, stdout);
    ++start;
  }
}

static void enable_() {
  setEnable("enable", true);
}

static void frame_() {
  // FIXME implement
  skipWhitespace();
  if (isAtEnd()) {
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

static void finish_() {
  debugger.isHalted = false;
  debugger.state = DBG_STEP_OUT;
}

static void ignore_() {
  skipWhitespace();
  if (isAtEnd() || !isdigit(*cmd)) {
    printf("Expect breakpoint nr after ignore cmd\n");
    return;
  }

  int nr = readInt();
  skipWhitespace();
  if (isAtEnd() || !isdigit(*cmd)) {
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
  debugger.state = DBG_NEXT;
  debugger.isHalted = false;
}

static void print_() {
  // FIXME implement
  skipWhitespace();
  if (isAtEnd()) {
    printf("Expect a expression as param to print.\n");
    return;
  }

  char *row;
  int len = readRestOfRow(&row);
  Value value;
  vm_eval(&value, row);
  printf("print (%s) = %s\n", row, valueToString(value)->chars);
  FREE_ARRAY(char, row, len);
}

static void quit_() {
  exit(0);
}

static void step_() {
  breakFrame = NULL;
  debugger.state = DBG_STEP;
  debugger.isHalted = false;
}

static void up_() {
  // FIXME implement
  printf("up\n");
}

static void watch_() {
  skipWhitespace();
  if (*cmd == '\0') {
    printf("Expect a expression as param to watch.\n");
    return;
  }

  char *row;
  int len = readRestOfRow(&row);

  printf("Setting watch %s\n", row);
  setWatchpointByExpr(row);
  FREE_ARRAY(char, row, len+1);
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
  { "echo", 4, echo_},
  { "enable", 6, enable_},
  { "en", 2, enable_},
  { "frame", 5, frame_},
  { "finish", 6, finish_},
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
  { "watch", 5, watch_},
  { "#", 1, comment_}
};

static void parseCommands(const char *commands) {
  if (!commands) return;
  cmd = commands;
  skipWhitespace();

next_cmd:
  while(!isAtEnd()) {
    for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); ++i) {
      if (memcmp(cmd, cmds[i].name, cmds[i].nameLen) == 0) {
        cmd += cmds[i].nameLen;
        cmds[i].parseFn();
        while (*cmd == '\n')
          cmd++; // eat up closing '\n'
        goto next_cmd;
      }
    }

    // not found, print error message
    char *row;
    int rowlen = readRestOfRow(&row);
    fprintf(stderr, "***Unrecognized command:%s\n", row);
    FREE_ARRAY(char, row, rowlen+1);
    break;
  }
}

// end command parser
// -----------------------------------------------------

void initBreakpoint(Breakpoint *breakpoint) {
  breakpoint->line =
    breakpoint->ignoreCount =
      breakpoint->hits = 0;

  breakpoint->module = NULL;
  breakpoint->next = NULL;
  breakpoint->condition = NULL;
  breakpoint->evalCondition = NULL;
  breakpoint->enabled = true;
}

void initWatchpoint(Watchpoint *watchpoint) {
  watchpoint->expr = NULL;
  watchpoint->next = NULL;
}

void freeBreakpoint(Breakpoint *breakpoint) {
  FREE(Breakpoint, breakpoint);
  FREE_ARRAY(char, (char*)breakpoint->condition,
             strlen(breakpoint->condition) + 1);
}

void freeWatchpoint(Watchpoint *watchpoint) {
  FREE_ARRAY(char, (char*)watchpoint->expr,
             strlen(watchpoint->expr) +1);
}

void initDebugger() {
  debugger.breakpoints = NULL;
  debugger.watchpoints = NULL;
  debugger.state = DBG_RUN;
  debugger.isHalted = false;
}

DebugStates debuggerState() {
  return debugger.state;
}

void setDebuggerState(DebugStates state) {
  debugger.state = state;
}

void setBreakpoint(Breakpoint *breakpoint) {
  Breakpoint *bp = debugger.breakpoints,
             **prev = &debugger.breakpoints;
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
  Breakpoint *bp = debugger.breakpoints;
  while (bp != NULL) {
    if (bp->module == module && bp->line == line)
      return bp;
  }
  return NULL;
}


// get breakpoint at index or NULL if none existing
Breakpoint *getBreakpointByIndex(int brkpntNr) {
  int nr = 0;
  Breakpoint *bp = debugger.breakpoints;
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
  Breakpoint *bp = debugger.breakpoints;
  while (bp != NULL) {
    if (bp == breakpoint)
      return idx;
    idx++;
    bp = bp->next;
  }
  return -1;
}

bool clearBreakpoint(Breakpoint *breakpoint) {
  Breakpoint *bp = debugger.breakpoints,
             **prev = &debugger.breakpoints;
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
  Watchpoint *wp = debugger.watchpoints,
            **prev = &debugger.watchpoints;
  while (wp != NULL) {
    if (strcmp(wp->expr, watchpoint->expr) == 0) {
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

void setWatchpointByExpr(const char *expr) {
  Watchpoint *wp = ALLOCATE(Watchpoint, 1);
  initWatchpoint(wp);
  wp->expr = ALLOCATE(const char, strlen(expr)+1);
  strcpy((char*)wp->expr, expr);

  // insert into list
  Watchpoint *w = debugger.watchpoints,
             **ptrToW = &debugger.watchpoints;
  for (;w != NULL; w = w->next)
    ptrToW = &w->next;
  *ptrToW = wp;
}

bool clearWatchpoint(Watchpoint *watchpoint) {
  Watchpoint *wp = debugger.watchpoints,
            **prev = &debugger.watchpoints;
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
bool clearWatchPointByIdent(const char *expr) {
  Watchpoint *wp = getWatchpoint(expr);
  if (wp != NULL)
    return clearWatchpoint(wp);
  return false;
}

// get the watchpoint looking at ident in module and closure
Watchpoint *getWatchpoint(const char *expr) {
  Watchpoint *wp = debugger.watchpoints;
  while (wp != NULL) {
    if (strcmp(wp->expr, expr) == 0) {
      return wp;
    }
  }
  return NULL;
}

void setInitCommands(const char *debuggerCmds) {
  initCommands = debuggerCmds;
}

void runInitCommands() {
  if (initCommands && *initCommands != '\0')
    parseCommands(initCommands);
}

void resumeDebugger() {
  debugger.isHalted = false;
}

void markDebuggerRoots(ObjFlags flags) {
  Breakpoint *bp = debugger.breakpoints;
  while (bp != NULL) {
    markObject(OBJ_CAST(bp->evalCondition), flags);
    bp = bp->next;
  }
}

void onNextTick() {
  switch (debugger.state) {
  case DBG_RUN: return;
  case DBG_STEP_OUT:
    return checkStepOut();
  case DBG_ARMED:
    return checkBreakpoints();
  case DBG_NEXT:
    return checkNext();
  case DBG_STEP: // fall through
  case DBG_HALT:
    debugger.isHalted = true;
    return processEvents();
  case DBG_STOP: break;
  }
}
