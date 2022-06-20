#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <getopt.h>

#include "common.h"

#include "chunk.h"
#include "debug.h"
#include "vm.h"
#include "scanner.h"
#include "object.h"
#include "module.h"
#include "debugger.h"
#include "memory.h"

static void printUsage() {
  printf("Lox programming language implementation.\n"
         "usage: clox -dDvh file1.lox [file2.lox file3.lox ... ]\n"
         "clox                   open in interactive (REPL) mode.\n\n"
         "clox  -D debugCommandsFile scriptfile.lox\n\n"
         "clox  -v           Show version.\n\n"
         "clox  -h           Show help");
}

static int runFile(const char* path) {
  Module *module = createModule("__main__", path);
  InterpretResult result = loadModule(module);

  delModuleVM(module);

  switch (result) {
  case INTERPRET_COMPILE_ERROR: exit(65);
  case INTERPRET_RUNTIME_ERROR: exit(70);
  case INTERPRET_OK: return 1;
  default: return 0; // failed
  }
}

static char *
repl_completion_generator(const char *text, int state) {
  static int list_index, globals_index, len;
  const char *name = NULL;

  if (!state) {
    list_index = globals_index = 0;
    len = strlen(text);
  }

  // complete keywords
  if (list_index < keywordCnt) {
    while((name = keywords[list_index++])) {
      if (strncmp(name, text, len) == 0)
        return strdup(name);
    }
  }

  ValueArray globalKeys = tableKeys(&vm.globals);
  for (int i = 0, m = 0; i < globalKeys.count; ++i) {
    const char *key = AS_CSTRING(globalKeys.values[i]);
    if (strncmp(key, text, len) == 0) {
      if (((globals_index) - (m++)) == 0) {
        globals_index++;
        freeValueArray(&globalKeys);
        return strdup(key);
      }
    }
  }

  freeValueArray(&globalKeys);
  return NULL;
}

static char **
repl_completion(const char *text, int start, int end) {
  rl_attempted_completion_over = 1;
  return rl_completion_matches(text, repl_completion_generator);
}

static void repl() {
  initVM();
  Module *module = getCurrentModule();

  rl_attempted_completion_function = repl_completion;
  rl_completer_word_break_characters = " .";

  for(;;) {
    char *buffer = readline("> ");
    if (buffer != NULL) {
      if (strlen(buffer) > 0) {
        add_history(buffer);
        if (compileModule(module, buffer))
          interpretModule(module);
      }

      free(buffer);
    }
  }
}

int main(int argc, char * const argv[]) {

  DebugStates initDbgState = DBG_RUN;
  const char *initDebuggerCmds = NULL;
  loxInit(argc, argv);

  if (argc == 1) {
    repl();
  } else {
    int opt = 1; char *dbgCmdsFile = NULL;

    while ((opt = getopt(argc, argv, "dD:hv")) != -1) {
      switch (opt) {
      case 'd':
        initDbgState = DBG_HALT;
        break;
      case 'D':
        dbgCmdsFile = optarg;
        while (isspace(*dbgCmdsFile)) dbgCmdsFile++;
        if (!fileExists(dbgCmdsFile)) {
          fprintf(stderr, "***Debugger commands file not found %s.\n", dbgCmdsFile);
          return 74;
        }
        initDbgState = DBG_HALT;
        initDebuggerCmds = readFile(dbgCmdsFile);
        setInitCommands(initDebuggerCmds);
        break;
      case 'h':
        printUsage();
        return 0;
      case 'v':
        printf("lox version %s", LOX_VERSION);
        return 0;
      default:
        break;
      }
    }

    for (; optind < argc; optind++) {
      initVM();
      setDebuggerState(initDbgState);
      if (!runFile(argv[optind]))
        exit(70);
    }

  }

  freeVM();
  if (initDebuggerCmds != NULL)
    FREE_ARRAY(char, (char*)initDebuggerCmds, strlen(initDebuggerCmds));

  return 0;
}
