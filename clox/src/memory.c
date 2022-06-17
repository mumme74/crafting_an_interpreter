#include <stdlib.h>
#include <stdio.h>

#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
# include <stdio.h>
# include "debug.h"
# ifndef DEBUG_LOG_GC_MARK
#  define DEBUG_LOG_GC_MARK 1
# endif
# ifndef DEBUG_LOG_GC_FREE
#  define DEBUG_LOG_GC_FREE 1
# endif
#endif

#define GC_HEAP_GROW_FACTOR 2

// --------------------------------------------------------------
static void markArray(ValueArray* array, ObjFlags flags);
static bool disableGC = false;

static void freeObject(Obj *object) {
#if DEBUG_LOG_GC_FREE
  printf("%p free type %s\n", (void*)object, typeOfObject(object));
  if (object->type == OBJ_STRING)
    printf("string=%s flags=%x\n", ((ObjString*)object)->chars, object->flags);
#endif

  switch (object->type) {
  case OBJ_BOUND_METHOD:
    FREE(ObjBoundMethod, object); break;
  case OBJ_CLASS: {
    ObjClass *klass = (ObjClass*)object;
    freeTable(&klass->methods);
    FREE(ObjClass, klass);
  } break;
  case OBJ_DICT: {
    ObjDict *dict = (ObjDict*)object;
    freeTable(&dict->fields);
    FREE(ObjDict, dict);
  } break;
  case OBJ_CLOSURE: {
    ObjClosure *closure = (ObjClosure*)object;
    FREE_ARRAY(ObjUpvalue*, closure->upvalues,
              closure->upvalueCount);
    FREE(ObjClosure, object);
  } break;
  case OBJ_FUNCTION: {
    ObjFunction *function = (ObjFunction*)object;
    freeChunk(&function->chunk);
    FREE(ObjFunction, object);
  } break;
  case OBJ_INSTANCE: {
    ObjInstance *instance = (ObjInstance*)object;
    freeTable(&instance->fields);
    FREE(ObjInstance, object);
  } break;
  case OBJ_NATIVE:
    FREE(ObjNative, object); break;
  case OBJ_STRING: {
    ObjString *string = (ObjString*)object;
    FREE_ARRAY(char, string->chars, string->length +1);
    FREE(ObjString, object);
  } break;
  case OBJ_UPVALUE:
    FREE(ObjUpvalue, object); break;
  }
}

// for the GC
static void blackenObject(Obj *object, ObjFlags flags) {
#if DEBUG_LOG_GC_MARK
  printf("%p blacken %s\n", (void*)object,
         objectToString(OBJ_VAL(object))->chars);
#endif

  switch (object->type) {
  case OBJ_BOUND_METHOD: {
    ObjBoundMethod *bound = (ObjBoundMethod*)object;
    markValue(bound->reciever, flags);
    markObject((Obj*)bound->methods, flags);
  } break;
  case OBJ_DICT: {
    ObjDict *dict = (ObjDict*)object;
    markTable(&dict->fields, flags);
  } break;
  case OBJ_CLASS: {
    ObjClass *klass = (ObjClass*)object;
    markObject(OBJ_CAST(klass->name), flags);
    markTable(&klass->methods, flags);
  } break;
  case OBJ_CLOSURE: {
    ObjClosure* closure = (ObjClosure*)object;
    markObject((Obj*)closure->function, flags);
    for (int i = 0; i < closure->upvalueCount; ++i) {
      markObject((Obj*)closure->upvalues[i], flags);
    }
  } break;
  case OBJ_FUNCTION: {
    ObjFunction* function = (ObjFunction*)object;
    markObject((Obj*)function->name, flags);
    markArray(&function->chunk.constants, flags);
  } break;
  case OBJ_INSTANCE: {
    ObjInstance *instance = (ObjInstance*)object;
    markObject((Obj*)instance->klass, flags);
    markTable(&instance->fields, flags);
  } break;
  case OBJ_UPVALUE:
    markValue(((ObjUpvalue*)object)->closed, flags);
    break;
  case OBJ_NATIVE:
    markObject(OBJ_CAST(((ObjNative*)object)->name), flags);
    break;
  case OBJ_STRING:
    break;
  }
}

static void markArray(ValueArray* array, ObjFlags flags) {
  for (int i = 0; i < array->count; ++i) {
    markValue(array->values[i], flags);
  }
}

static void markRoots(ObjFlags flags) {
  markRootsVM(flags);
  markCompilerRoots(flags);
  markDebuggerRoots(flags);
}

static void traceReferences(ObjFlags flags) {
  while (vm.grayCount > 0) {
    Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object, flags);
  }
}

static void traceOlderReferences(ObjFlags flags) {
  Obj* object = vm.olderObjects;
  while (object != NULL) {
    blackenObject(object, flags | GC_IS_OLDER);
    object = object->next;
  }
}

static void sweep(Obj** sweepList, ObjFlags flags) {
  Obj *previous = NULL,
      *object = *sweepList;
  while (object != NULL) {
    if ((object->flags & GC_FLAGS) >= flags) {
      object->flags &= ~flags;
      previous = object;
      object = object->next;
    } else {
      Obj* unreached = object;
      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        *sweepList = object;
      }

      freeObject(unreached);
    }
  }
}

static void moveGenList(Obj** fromList, Obj** toList,
                        ObjFlags removeFlags, ObjFlags setFlags)
{
  Obj **toPtr = *toList != NULL ? &(*toList)->next : toList;
  if (*fromList) {
    *toPtr = *fromList;
    *fromList = NULL;
  }

  Obj* object = *toList;
  while (object != NULL) {
    object->flags &= ~removeFlags;
    object->flags |= setFlags;
    object = object->next;
  }

  vm.olderBytesAllocated += vm.infantBytesAllocated;
  vm.infantBytesAllocated = 0;
}

static void checkGC() {
  if (disableGC) return;

#ifdef DEBUG_STRESS_GC
  infantGarbageCollect();
#endif

  if (vm.infantBytesAllocated > vm.infantNextGC) {
    infantGarbageCollect();
  }
}

 // ---------------------------------------------------------------

void *reallocate(void *pointer, size_t oldSize, size_t newSize) {
  if (pointer != NULL && ((Obj*)pointer)->flags & GC_IS_OLDER)
    vm.olderBytesAllocated += newSize - oldSize;
  else
    vm.infantBytesAllocated += newSize - oldSize;

  if (newSize > oldSize)
    checkGC();

  if (newSize == 0) {
#if DEBUG_LOG_GC_FREE
    printf("free %p %zu bytes\n", pointer, newSize -oldSize);
#endif
    free(pointer);
    return NULL;
  }

  void *result = realloc(pointer, newSize);
  if (result == NULL) exit(1);
  return result;
}

void markObject(Obj *object, ObjFlags flags) {
  if (object == NULL ||
      (object->flags & GC_FLAGS) >= flags)
    return;

#if DEBUG_LOG_GC_MARK
  printf("%p mark %s curflags%x setFlags:%x\n",
        (void*)object,
        objectToString(OBJ_VAL(object))->chars,
        object->flags, flags);
#endif

  object->flags |= flags;

  if (vm.grayCapacity < vm.grayCount +1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack = (Obj**)realloc(vm.grayStack,
                            sizeof(Obj*) * vm.grayCapacity);
    if (vm.grayStack == NULL) {
      fprintf(stderr, "Failed to allocate working memory during GC run.");
      exit(1);
    }
  }

  vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value, ObjFlags flags) {
  if (IS_OBJ(value)) markObject(AS_OBJ(value), flags);
}

void freeObjects() {
  Obj *lists[] = { vm.infantObjects, vm.olderObjects };

  for (int i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
    Obj *object = lists[i];
    while (object != NULL) {
      Obj *next = object->next;
      freeObject(object);
      object = next;
    }
  }

  free(vm.grayStack);
}

bool setGCenabled(bool enable) {
  bool enabled = !disableGC;
  disableGC = !enable;
  if (enable) checkGC();
  return enabled;
}

void infantGarbageCollect() {
  disableGC = false;
#ifdef DEBUG_LOG_GC
  printf("-- gc begin infant collect\n");
  size_t before = vm.infantBytesAllocated;
#endif

  markRoots(GC_IS_MARKED);
  traceReferences(GC_IS_MARKED);
  traceOlderReferences(GC_IS_MARKED);
  sweepVM(GC_IS_MARKED);
  sweep(&vm.infantObjects, GC_IS_MARKED);
  moveGenList(&vm.infantObjects, &vm.olderObjects,
              GC_IS_MARKED, GC_IS_OLDER);

#ifdef DEBUG_STRESS_GC_OLDER
  olderGarbageCollect();
#endif

  vm.infantNextGC = vm.infantBytesAllocated > INFANT_GC_MIN ?
    vm.infantBytesAllocated * GC_HEAP_GROW_FACTOR : INFANT_GC_MIN;

  if (vm.infantBytesAllocated + vm.olderBytesAllocated >
      vm.infantNextGC + vm.olderNextGC)
  {
    olderGarbageCollect();
  }

#ifdef DEBUG_LOG_GC
  printf("-- gc end infant collect\n");
  printf("   collected %zu bytes (from %zu to %zu) next as %zu\n",
        before - vm.infantBytesAllocated, before, vm.infantBytesAllocated,
        vm.infantNextGC);
#endif

  disableGC = true;
}

void olderGarbageCollect() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin older collect\n");
  size_t before = vm.olderBytesAllocated;
#endif

markRoots(GC_IS_MARKED_OLDER);
traceReferences(GC_IS_MARKED_OLDER);
sweepVM(GC_IS_MARKED);
sweep(&vm.olderObjects, GC_IS_MARKED_OLDER);

vm.olderNextGC = vm.olderBytesAllocated > OLDER_GC_MIN ?
  vm.olderBytesAllocated * GC_HEAP_GROW_FACTOR : OLDER_GC_MIN;

#ifdef DEBUG_LOG_GC
  printf("-- gc end older collect\n");
  printf("   collected older %zu bytes (from %zu to %zu) next as %zu\n",
        before - vm.olderBytesAllocated, before, vm.olderBytesAllocated,
        vm.olderNextGC);
#endif
}

