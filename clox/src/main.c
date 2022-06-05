#include <stdio.h>
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


static char *
readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }
  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char *buffer = (char*)malloc(fileSize +1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }
  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }

  buffer[bytesRead] = '\0';

  fclose(file);
  return buffer;
}

static int
runFile(const char* path) {
  char *source = readFile(path);
  InterpretResult result = interpret(source);
  free(source);

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

static void
repl() {
  initVM();
  rl_attempted_completion_function = repl_completion;
  rl_completer_word_break_characters = " .";

  for(;;) {
    char *buffer = readline("> ");
    if (buffer != NULL) {
      if (strlen(buffer) > 0) {
        add_history(buffer);
        interpret(buffer);
      }

      free(buffer);
    }
  }
  freeVM();
}

int
main(int argc, const char *argv[]) {

  if (argc == 1) {
    repl();
  } else {
    for (int i = 1; i < argc; i++) {
      initVM();
      if (!runFile(argv[i]))
        exit(70);
      freeVM();
    }
  }

  return 0;
}