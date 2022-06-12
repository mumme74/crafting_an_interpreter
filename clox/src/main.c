#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "common.h"

#include "chunk.h"
#include "debug.h"
#include "vm.h"
#include "scanner.h"
#include "object.h"
#include "module.h"
#include "debugger.h"


static int runFile(const char* path) {
  Module *module = createModule("__main__");

  InterpretResult result = loadModule(module, path);

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

  ValueArray globalKeys = tableKeys(&vm.currentModule->globals);
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
  Module *module = vm.currentModule;

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

int
main(int argc, const char *argv[]) {

  if (argc == 1) {
    repl();
  } else {
    int i = 1;
    DebugCB cb = NULL;
    if (argc >= 2 && strcmp(argv[2], "-d")) {
      i++;
      initDebugger();
      setDebuggerState(DBG_HALT);
      cb = onNextTick;
    }

    for (; i < argc; i++) {
      initVM();
      if (cb) vm.debugCB = cb;
      if (!runFile(argv[i]))
        exit(70);
    }

  }
  freeVM();

  return 0;
}