#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

#include "chunk.h"
#include "debug.h"
#include "vm.h"


static char *readFile(const char* path) {
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

static int runFile(const char* path) {
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

static void repl() {
  char line[1024];
  for(;;) {
    printf("> ");

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }

    interpret(line);
  }
}

int main(int argc, const char *argv[]) {
  initVM();

  if (argc == 1) {
    repl();
  } else {
    for (int i = 1; i < argc; i++) {
      if (!runFile(argv[i]))
        exit(70);
    }
  }

  freeVM();
  return 0;
}