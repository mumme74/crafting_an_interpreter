
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "memory.h"
#include "object.h"

static char * const *largv = NULL;
static int largc = 0;

// --------------------------------------------------------------

char *readFile(const char *path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }
  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char *buffer = ALLOCATE(char, fileSize +1);
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

bool fileExists(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (fp != NULL) {
    fclose(fp);
    return true;
  }
  return false;
}

void loxInit(int argc, char * const argv[]) {
  largv = argv;
  largc = argc;

(void)largv;
(void)largc;
}

void initTypes() {
  initObjectsModule();
  initArrayModule();
}

void freeTypes() {
  freeObjectsModule();
  freeArrayModule();
}
