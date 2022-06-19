#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

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
static int line = 0,
           listLineNr =-1;
static const char *initCommands = NULL;
static bool silentMode = false;
static FILE *outstream = NULL,
            *errstream = NULL,
            *instream  = NULL;

static void parseCommands(const char *buffer);

static void fprintOut(FILE *stream, const char *frmt, ...) {
  va_list args;
  va_start(args, frmt);
  if (!silentMode)
    vfprintf(stream, frmt, args);
  va_end(args);
}

/*static void fputsOut(const char *string, FILE *stream) {
  if (!silentMode)
    fputs(string, stream);
}*/

static void fputcOut(const char c, FILE *stream) {
  if (!silentMode)
    fputc(c, stream);
}

// prints the source around baseline,
// window = how many rows before and after
static void printSource(int baseline, int window) {
  if (baseline < 1) baseline = line;

  int fromLine = baseline -window < 0 ? 0 : baseline -window,
      toLine   = baseline +window;
  int lineCnt  = 1;

  Module *module = getCurrentModule();
  const char *src = module->source;
  fputcOut('\n', outstream);

  while (*src != '\0') {
    if (*src == '\n') ++lineCnt;
    if (lineCnt >= fromLine && lineCnt <= toLine) {
      if (*src == '\n' || src == module->source) {
        fprintOut(outstream, "\n%-4d%s ", lineCnt, (lineCnt == line ? "*": " "));
        if (src == module->source) putc(*src, outstream);
      } else
        fputcOut(*src, outstream);
    } if (lineCnt > toLine) break;
    src++;
  }
  fputcOut('\n', outstream);
}

static void printWatchpoints() {
  Watchpoint *wp = debugger.watchpoints;
  for (;wp != NULL; wp = wp->next) {
    Value value;
    if (vm_eval(&value, wp->expr) == INTERPRET_OK &&
        !IS_NIL(value))
    {
      fprintOut(outstream, " %s:%s\n", wp->expr, valueToString(value)->chars);
    }
  }
}

static void runBreakpointCmds(Breakpoint *bp) {
  if (bp->commands != NULL) {
    bool silent = silentMode;
    silentMode = bp->silenceCmds;
    parseCommands(bp->commands);
    silentMode = silent;
  }
}

static void processEvents() {

  printWatchpoints();
  static char *prev = NULL;

  rl_completer_word_break_characters = " .";
  while (debugger.isHalted) {
    fprintOut(outstream, "**** debugger interface ****\n");
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

static void setCurrentFrame(int stackLevel) {
  frame = &vm.frames[vm.frameCount -1 - stackLevel];
  line = frame->closure->function->chunk.lines[
    (int)(frame->ip - frame->closure->function->chunk.code)];
  listLineNr = -1;
}

static void checkStepOut() {
  frame = &vm.frames[vm.frameCount -1];
  if (*(frame->ip-1) == OP_RETURN) {
    setCurrentFrame(0);
    debugger.isHalted = true;
    debugger.state = DBG_NEXT;
    printSource(line, 0);
    processEvents();
  }
}


static void checkBreakpoints() {
  setCurrentFrame(0);

  Module *module = getCurrentModule();
  Breakpoint *bp = debugger.breakpoints;
  for (int nr = 1; bp != NULL; ++nr, bp = bp->next) {
    if (bp->module == module &&
        bp->line == line)
    {
      if (!bp->enabled) return;
      if (bp->condition) {
        if (!bp->evalCondition) {
          if (vm_evalBuild(&bp->evalCondition, bp->condition) !=
              INTERPRET_OK) {
            fprintOut(outstream, "Breakpoint %d condition invalid.(%s)\n",
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
        fprintOut(outstream, "\n* stopped at breakpoint %d in %s\n* file:%s\n",
               nr, module->name->chars,
               module->path->chars);
        printSource(line, 2);
        runBreakpointCmds(bp);
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

// each command in parser stores one of these
typedef struct CmdTbl {
  const char *name;
  int nameLen;
  void (*parseFn)();
} CmdTbl;

// key-value pair, must be deallocated manually
typedef struct {
  char *key;
  Value value;
} KeyVal;

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
  static char buf[100] = {0};
  char *pbuf = buf;
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

// lookup and create a new KeyVal from locals
static void getKeyVal(KeyVal **pkeyVal, Local *loc, Value *value)
{
  (*pkeyVal)->key = ALLOCATE(char, loc->name.length);
  memcpy((*pkeyVal)->key, loc->name.start, loc->name.length);
  (*pkeyVal)->key[loc->name.length] = '\0';
  (*pkeyVal)->value = *value;
  (*pkeyVal)++;
}

// compare keys in KeyVal, used for sorting
static int compareKeyVal(const void *a, const void *b) {
  const KeyVal *aKeyVal = (const KeyVal*)a,
               *bKeyVal = (const KeyVal*)b;
  return strcmp(aKeyVal->key, bKeyVal->key);
}

static void nfo_brk() {
  fprintOut(outstream, "breakpoint info\n");
  Breakpoint *bp = debugger.breakpoints;
  for (int nr = 1; bp != NULL; bp = bp->next, nr++) {
    fprintOut(outstream, "[%d] breakpoint at %s:%d\n", nr,
           bp->module->path->chars, bp->line);
    fprintOut(outstream, "      hits:%d ignoreCount:%d enabled:%d\n",
           bp->hits, bp->ignoreCount, bp->enabled);
    if (bp->condition != NULL)
      fprintOut(outstream, "      condition:%s\n", bp->condition);
  }
}

static void nfo_wtch() {
  fprintOut(outstream, "watchpoint info\n");
  Watchpoint *wp = debugger.watchpoints;
  for (int nr = 1; wp != NULL; wp = wp->next, nr++) {
    fprintOut(outstream, "[%d] watchpoint expr:%s\n", nr, wp->expr);
  }
}

static void nfo_frm() {
  fprintOut(outstream, "info frame\n");
  int stackLvl = 0;
  for (; stackLvl < vm.frameCount; ++ stackLvl)
    if (&vm.frames[vm.frameCount - 1 - stackLvl] == frame) break;

  fprintOut(outstream, "Stack level #%d frame '%s' in module '%s'\n"
         " at '%s'\n at line:%d\n",
    stackLvl,
    frame->closure->function->name->chars,
    frame->closure->function->chunk.module->name->chars,
    frame->closure->function->chunk.module->path->chars,
    line
  );
}

static void nfo_loc() {
  fprintOut(outstream, "info locals\n");
  Compiler *compiler = frame->closure->function->chunk.compiler;
  KeyVal *keyValues = ALLOCATE(KeyVal, compiler->localCount +
                               frame->closure->upvalueCount);
  KeyVal *pkeyVal = keyValues;

  // get locals
  for (int i = 0; i < compiler->localCount; ++i) {
    Local *loc = &compiler->locals[i];
    if (loc->name.length > 0)
      getKeyVal(&pkeyVal, loc, &frame->slots[i]);
  }

  // get upvalues
  for (int i = 0; i < frame->closure->upvalueCount; ++i) {
    int index = i;
    ObjFunction *func = compiler->function;
    Local *loc = getUpvalueByIndex(&func, &index);
    if (loc->name.length > 0)
      getKeyVal(&pkeyVal, loc,
                frame->closure->upvalues[i]->location);
  }

  // sort them
  qsort(keyValues, pkeyVal - keyValues,
        sizeof(KeyVal), compareKeyVal);

  // output values
  for (int i = 0; i < pkeyVal - keyValues; ++i) {
    fprintOut(outstream, "[%s] %12s = %s\n",
           typeOfValue(keyValues[i].value),
           keyValues[i].key,
           valueToString(keyValues[i].value)->chars);
    FREE_ARRAY(char, keyValues[i].key, strlen(keyValues[i].key));
  }

  FREE_ARRAY(KeyVal, keyValues, compiler->localCount);
}

static void nfo_gbl() {
  fprintOut(outstream, "info globals\n");

  KeyVal *keyVal = ALLOCATE(KeyVal, vm.globals.count);
  ValueArray globalKeys = tableKeys(&vm.globals);

  // get globals
  for (int i = 0; i < globalKeys.count; ++i) {
    keyVal[i].key = AS_CSTRING(globalKeys.values[i]);
    tableGet(&vm.globals, AS_STRING(globalKeys.values[i]), &keyVal[i].value);
  }

  // sort them alpabetically
  qsort(keyVal, globalKeys.count,
        sizeof(KeyVal), compareKeyVal);

  // print globals
  for (int i = 0; i < globalKeys.count; ++i) {
    fprintOut(outstream, "[%-12s] %s:%s\n",
           typeOfValue(keyVal[i].value),
           keyVal[i].key,
           valueToString(keyVal[i].value)->chars);
  }

  FREE_ARRAY(KeyVal, keyVal, vm.globals.count);
  freeValueArray(&globalKeys);
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

  fprintOut(outstream, "Unrecognized info cmd %s\n", readWord());
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
    "backtrace nr    Print backtrace, limit to nr.\n"
  },{
    "bt", 2,
    "bt              Shorthand for backtrace\n"
  },{
    "break", 5,
    "break           Sets a breakpoint at current line.\n"
    "break line      Sets a breakpoint at line in current file.\n"
    "break file:line Sets a breakpoint at line in file"
  },{
    "b",1,
    "b               Shorthand for break.\n"
  },{
    "clear", 5,
    "clear           Clears all breakpoints.\n"
    "clear nr        Clears breakpoint with number nr\n"
  },{
    "commands", 8,
    "commands nr     Specify commands that should run each time a \n"
    "                breakpoint nr triggers, if silent prevent printout\n"
    "commands nr [silent] \n...list of commands\nend\n"
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
    "end", 3,
    "end             Ends a command list for a breakpoint\n"
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
    "list", 4,
    "list            Show next 10 lines of code\n"
    "list -          Show previous 10 lines of code\n"
    "list nr         Show 10 lines surrounding line at nr\n"
  },{
    "l", 1,
    "l                Shorthand for list\n"
    "                 See list for more details\n"
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
      fprintOut(outstream,"\n%s", hlpInfos[i].msg);
    }
    return;
  }

  for (int i = 0; i < sizeof(hlpInfos) / sizeof(hlpInfos[0]); ++i) {
    if (memcmp(cmd, hlpInfos[i].name, hlpInfos[i].nameLen) == 0) {
      fprintOut(outstream,"%s", hlpInfos[i].msg);
      cmd += hlpInfos[i].nameLen;
      return;
    }
  }
  fprintOut(outstream,"Unrecognized command to help %s\n", readWord());
}

static void backtrace_() {
  fprintOut(outstream, "backtrace\n");
  skipWhitespace();
  int limit = vm.frameCount;
  if (!isAtEnd()) {
    limit -= readInt();
    if (limit < 1) {
      fprintOut(outstream,"Invalid limit\n");
      return;
    }
  }

  for (int i = 0; i < limit; ++i) {
    CallFrame *frm = &vm.frames[vm.frameCount - 1 - i];
    const char *fnName = frm->closure->function->name != NULL ?
      frm->closure->function->name->chars : "<script>";
    fprintOut(outstream,"#%d %s at %s at %s:%d\n",
          i,
          frm == frame ? "*" : " ",
          fnName,
          frm->closure->function->chunk.module->path->chars,
          frm->closure->function->chunk.lines[
            (int)(frm->ip - frm->closure->function->chunk.code)]
    );
  }
}

static void readLineAndPath(int *lnNr, const char **path) {
  skipWhitespace();
  *path = getCurrentModule()->path->chars;
  *lnNr = line;
  if (!isAtEnd()) {
    if (isalpha(*cmd)) {
      *path = readPath();
      if (*cmd != ':') {
        fprintOut(outstream,"Expected ':' between file and linenr, but got: %c.\n", *cmd);
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
    fprintOut(outstream,"Module with path:%s not loaded.\n", path);
    return;
  }
  setBreakpointAtLine(lnNr, mod);
  fprintOut(outstream,"Set breakpoint at %s:%d\n", path, lnNr);
}

static void clear_() {
  int lnNr;
  const char *path;
  readLineAndPath(&lnNr, &path);
  Module *mod = getModule(path);
  if (!mod) {
    fprintOut(outstream, "Module with path:%s not loaded.\n", path);
    return;
  }
  if (clearBreakpointAtLine(lnNr, mod)) {
    fprintOut(outstream, "Cleared breakpoint at %s:%d\n", path, lnNr);
  } else {
    fprintOut(outstream, "Breakpoint not found, %s:%d \n", path, line);
  }

}

// add commands that run each time breakpoint triggers
static void commands_() {
  skipWhitespace();
  if (isAtEnd()) {
    fprintOut(errstream, "Expects a breakpoint nr.\n");
    return;
  }
  int bpNr = readInt();
  Breakpoint *bp = getBreakpointByIndex(bpNr);
  if (bp == NULL) {
    fprintOut(errstream, "Breakpoint %d not found\n", bpNr);
    return;
  }

  // optional silent keyword
  skipWhitespace();
  const char *silentWord = readWord();
  bool silent = silentWord != NULL &&
                    strcmp(silentWord, "silent") == 0;
  skipWhitespace();

  // look for end
  if (*cmd == '\n') cmd++;
  const char *endPos = NULL, *start = cmd;
  bool cleanLine = true;
  for (; *cmd != '\0'; cmd++) {
    // if starts with e on a fresh line
    if (cleanLine && *cmd == 'e') endPos = cmd;
    else if (endPos != NULL && cmd - endPos == 3) {
      if (memcmp(endPos, "end", 3) == 0) break;
      endPos = NULL;
    }
    // if a new line
    if (*cmd == '\n')
      cleanLine = true;
    else if (!isspace(*cmd) && cleanLine)
      cleanLine = false;
  }

  if (endPos == NULL) {
    fprintOut(errstream, "End not found in commands list.\n");
    return;
  }

  if (bp->commands != NULL)
    FREE_ARRAY(char, (char*)bp->commands, strlen(bp->commands));

  char *commands = ALLOCATE(char, (size_t)(cmd - start +1));
  memcpy(commands, start, endPos - start);
  commands[(size_t)(cmd - start)] = '\0';

  bp->commands = commands;
  bp->silenceCmds = silent;

}

static void comment_() {
  while (!isAtEnd())
    ++cmd;
}

static void cond_() {
  skipWhitespace();
  if (isAtEnd() || !isdigit(*cmd)) {
    fprintOut(outstream, "Expect breakpoint nr after 'cond'.\n");
    return;
  }

  int nr = readInt();
  Breakpoint *bp = getBreakpointByIndex(nr);
  if (bp == NULL) {
    fprintOut(outstream,"Breakpoint %d not found.\n", nr);
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
    fprintOut(outstream,"Cleared condition for breakpoint %d at %s:%d.\n",
          nr, bp->module->path->chars, bp->line);
    return;
  }

  char *cond;
  readRestOfRow(&cond);

  if (bp->condition != NULL)
    FREE_ARRAY(char, (char*)bp->condition, strlen(bp->condition)+1);
  bp->condition = cond;

  fprintOut(outstream,"condtion %s set for breakpoint %d at %s:%d\n",
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
    fprintOut(outstream,"Expects breakpoint nr after delete command.\n");
    return;
  }

  clearBreakpoint(getBreakpointByIndex(readInt()));
  fprintOut(outstream,"Delete breakpoint.");
}

static void setEnable(const char *cmd, bool state) {
  skipWhitespace();
  if (isAtEnd()) {
    if (!isdigit(*cmd)) {
      fprintOut(outstream,"Expect breakpoint nr after %s command.\n", cmd);
    }
    int nr = readInt();
    Breakpoint *bp = getBreakpointByIndex(nr);
    if (!bp) {
      fprintOut(outstream,"Breakpoint %d not found.\n", nr);
      return;
    }
    bp->enabled = state;
    fprintOut(outstream,"Set breakpoint %sed.\n", cmd);
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
  int stackLvl = 0;
  for (; stackLvl < vm.frameCount; ++stackLvl) {
    if (&vm.frames[vm.frameCount-1 - stackLvl] == frame)
      break;
  }

  stackLvl = stackLvl < vm.frameCount -1 ? stackLvl +1 : stackLvl;
  fprintOut(outstream,"down to frame #%d\n", stackLvl);
  setCurrentFrame(stackLvl);
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
      fputcOut('\n', outstream);
    else
      fputcOut(*start, outstream);
    ++start;
  }
}

static void enable_() {
  setEnable("enable", true);
}

static void frame_() {
  int stackLvl = 0;
  skipWhitespace();
  if (!isAtEnd()) {
    if (!isdigit(*cmd)) {
      fprintOut(outstream,"Expect nr after frame\n");
      return;
    }
    stackLvl = readInt();
  }

  int frameIdx = vm.frameCount -1 - stackLvl;
  if (frameIdx < 0){
    fprintOut(outstream,"Invalid frame nr.\n");
    return;
  }

  fprintOut(outstream,"Select frame %d\n", stackLvl);
  setCurrentFrame(stackLvl);
}

static void finish_() {
  debugger.isHalted = false;
  debugger.state = DBG_STEP_OUT;
}

static void ignore_() {
  skipWhitespace();
  if (isAtEnd() || !isdigit(*cmd)) {
    fprintOut(outstream,"Expect breakpoint nr after ignore cmd\n");
    return;
  }

  int nr = readInt();
  skipWhitespace();
  if (isAtEnd() || !isdigit(*cmd)) {
    fprintOut(outstream,"Expect ignore count after breakpoint nr.\n");
    return;
  }

  int hits = readInt();

  Breakpoint *bp = getBreakpointByIndex(nr);
  if (bp) {
    bp->ignoreCount = hits;
  }
}

static void list_() {
  skipWhitespace();
  int lineNr = listLineNr == -1 ? line +5 : listLineNr + 10;
  listLineNr = lineNr;
  if (!isAtEnd()) {
    if (*cmd == '-') {
      lineNr = lineNr -20 > 1 ? lineNr -20 : 1;
      listLineNr = lineNr;
      cmd++;
    }
    else if (isdigit(*cmd))
      listLineNr = lineNr = readInt();
  }
  printSource(lineNr, 5);
}

static void next_() {
  breakFrame = frame;
  debugger.state = DBG_NEXT;
  debugger.isHalted = false;
}

static void print_() {
  skipWhitespace();
  if (isAtEnd()) {
    fprintOut(outstream,"Expect a expression as param to print.\n");
    return;
  }

  char *row;
  int len = readRestOfRow(&row);
  Value value;
  vm_eval(&value, row);
  // this printout should print event though silent mode is active
  fprintf(outstream,"print (%s) = %s\n", row, valueToString(value)->chars);
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
  int stackLvl = 0;
  for (; stackLvl < vm.frameCount; ++stackLvl) {
    if (&vm.frames[vm.frameCount-1 - stackLvl] == frame)
      break;
  }

  stackLvl = stackLvl > 0 ? stackLvl -1 : 0;
  fprintOut(outstream,"up to frame #%d\n", stackLvl);
  setCurrentFrame(stackLvl);
}

static void watch_() {
  skipWhitespace();
  if (*cmd == '\0') {
    fprintOut(outstream,"Expect a expression as param to watch.\n");
    return;
  }

  char *row;
  int len = readRestOfRow(&row);

  fprintOut(outstream,"Setting watch %s\n", row);
  setWatchpointByExpr(row);
  FREE_ARRAY(char, row, len+1);
}

const CmdTbl cmds[] = {
  { "backtrace", 9, backtrace_ },
  { "bt", 2, backtrace_ },
  { "break", 5, break_  },
  { "b", 1, break_},
  { "clear", 5, clear_},
  { "cond", 4, cond_},
  { "continue", 8, continue_},
  { "c", 1, continue_},
  { "commands", 8, commands_},
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
  { "list", 4, list_},
  { "l", 1, list_},
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
    skipWhitespace();
    const char *cmdWd = *cmd == '#' ? "#" : readWord();
    int len = strlen(cmdWd);
    for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); ++i) {
      if (len == cmds[i].nameLen &&
          memcmp(cmdWd, cmds[i].name, cmds[i].nameLen) == 0)
      {
        //cmd += cmds[i].nameLen;
        cmds[i].parseFn();
        while(*cmd != '\n' && *cmd != '\0') cmd++; // trim unwanted stuff
        while (*cmd == '\n') cmd++; // eat up closing '\n'
        goto next_cmd;
      }
    }

    // not found, print error message
    char *row;
    int rowlen = readRestOfRow(&row);
    fprintOut(errstream, "***Unrecognized command: '%s%s'\n", cmdWd, row);
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
  breakpoint->commands = NULL;
  breakpoint->silenceCmds = false;
}

void initWatchpoint(Watchpoint *watchpoint) {
  watchpoint->expr = NULL;
  watchpoint->next = NULL;
}

void freeBreakpoint(Breakpoint *breakpoint) {
  if (breakpoint->condition)
    FREE_ARRAY(char, (char*)breakpoint->condition,
               strlen(breakpoint->condition) + 1);
  if (breakpoint->commands)
    FREE_ARRAY(char, (char*)breakpoint->commands,
               strlen(breakpoint->commands) + 1);
  FREE(Breakpoint, breakpoint);
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

  setDebuggerIOStreams(stdin, stdout, stderr);
}

void setDebuggerIOStreams(FILE *in, FILE *out, FILE *err) {
  outstream = out;
  instream  = in;
  errstream = err;
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
  for (; bp != NULL; bp = bp->next) {
    if (bp->module == module && bp->line == line)
      return bp;
  }
  return NULL;
}


// get breakpoint at index or NULL if none existing
Breakpoint *getBreakpointByIndex(int brkpntNr) {
  Breakpoint *bp = debugger.breakpoints;
  for (int nr = 1;bp != NULL; nr++, bp = bp->next) {
    if (brkpntNr == nr)
      return bp;
  }
  return NULL;
}

int getBreakpointIndex(Breakpoint *breakpoint) {
  Breakpoint *bp = debugger.breakpoints;
  for (int nr = 1; bp != NULL; bp = bp->next, nr++) {
    if (bp == breakpoint)
      return nr;
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
    //return checkNext();
  case DBG_STEP: // fall through
  case DBG_HALT:
    debugger.isHalted = true;
    return processEvents();
  case DBG_STOP: break;
  }
}
