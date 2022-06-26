#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "common.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"
#include "object.h"
#include "memory.h"
#include "module.h"
#include "native.h"


VM vm; // global

// --------------------------------------------------------------

static bool failOnRuntimeErr = false;


static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}

static InterpretResult runtimeError(const char *format, ...) {
  if (failOnRuntimeErr) return INTERPRET_RUNTIME_ERROR;
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount -1; i >= 0; --i) {
    CallFrame *frame = &vm.frames[i];
    ObjFunction *function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.code -1;
    fprintf(stderr, "[line %d] in ",
            function->chunk.lines[instruction]);

    const char *fnname = function->name != NULL ?
                   function->name->chars : "script";
    fprintf(stderr, "%s\n", fnname);
  }

  resetStack();
  return INTERPRET_RUNTIME_ERROR;
}

static bool call(ObjClosure *closure, int argCount) {
  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.",
      closure->function->arity, argCount);
      return false;
  }

  CallFrame *frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop -argCount -1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod *bound = AS_BOUND_METHOD(callee);
      vm.stackTop[-argCount -1] = bound->reciever;
      return call(bound->methods, argCount);
    }
    case OBJ_CLASS: {
      ObjClass *klass = AS_CLASS(callee);
      vm.stackTop[-argCount -1] = OBJ_VAL((Obj*)newInstance(klass));
      Value initializer;
      if (tableGet(&klass->methods, vm.initString, &initializer)) {
        return call(AS_CLOSURE(initializer), argCount);
      } else if (argCount != 0) {
        runtimeError("Expected 0 arguments but got %d", argCount);
        return false;
      }
      return true;
    }
    case OBJ_CLOSURE:
      return call(AS_CLOSURE(callee), argCount);
    case OBJ_NATIVE_FN: {
      ObjNativeFn *nativeFn = AS_NATIVE_FN(callee);
      if (nativeFn->arity != argCount) {
        runtimeError("%s requires %d arguments.", nativeFn->name, nativeFn->arity);
        return false;
      }
      Value result = nativeFn->function(argCount, vm.stackTop - argCount);
      vm.stackTop -= argCount +1;
      push(result);
      return true;
    }
    case OBJ_NATIVE_METHOD: {
      ObjNativeMethod *nativeMethod = AS_NATIVE_METHOD(callee);
      if (nativeMethod->arity != argCount) {
        runtimeError("%s requires %d arguments.", nativeMethod->name->chars, nativeMethod->arity);
        return false;
      }
      Value *args = vm.stackTop - argCount;
      Value obj = vm.stackTop[-argCount -1];
      Value result = nativeMethod->method(obj, argCount, args);
      vm.stackTop -= argCount +1;
      push(result);
      return true;
    }
    case OBJ_NATIVE_PROP: case OBJ_PROTOTYPE:
    case OBJ_DICT: case OBJ_ARRAY:
    case OBJ_STRING: case OBJ_UPVALUE:
    case OBJ_INSTANCE: case OBJ_FUNCTION:
    case OBJ_MODULE: case OBJ_REFERENCE:
     break; // non callable object type
    }
  }

  runtimeError("Can only call functions and classes.");
  return false;
}

static bool invokeFromClass(ObjClass *klass, ObjString *name,
                            int argCount)
{
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }

  return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString *name, int argCount) {
  Value reciever = peek(argCount);
  Table *fields = NULL;
  Value value;

  if (IS_INSTANCE(reciever)) {
    fields = &AS_INSTANCE(reciever)->fields;
  } else if (IS_DICT(reciever)) {
    fields = &AS_DICT(reciever)->fields;
  }
  // lookup at native built in properties
  if (fields == NULL) {
    value = objMethodNative(AS_OBJ(reciever), name);
    if (!IS_NIL(value))
      return callValue(value, argCount);

    runtimeError("Method %s not found.", name->chars);
    return false;
  }

  if (tableGet(fields, name, &value)) {
    vm.stackTop[-argCount -1] = value;
    return callValue(value, argCount);
  } else if (IS_INSTANCE(reciever))
    return invokeFromClass(AS_INSTANCE(reciever)->klass, name, argCount);
  return false;
}

static bool bindMethod(ObjClass *klass, ObjString *name) {
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }

  ObjBoundMethod *bound =
    newBoundMethod(peek(0), AS_CLOSURE(method));

  pop();
  push(OBJ_VAL(OBJ_CAST(bound)));
  return true;
}

static ObjUpvalue *captureUpvalue(Value *local) {
  ObjUpvalue *prevUpvalue = NULL,
             *upvalue = vm.openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue *createdUpvalue = newUpvalue(local);
  createdUpvalue->next = upvalue;
  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

static void closeUpvalues(Value *last) {
  while (vm.openUpvalues != NULL &&
         vm.openUpvalues->location >= last)
  {
    ObjUpvalue *upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static void defineMethod(ObjString *name) {
  Value method = peek(0);
  ObjClass *klass = AS_CLASS(peek(1));
  tableSet(&klass->methods, name, method);
  pop();
}

static void concatenate() {
  ObjString *b = AS_STRING(peek(0)), // peek because of GC
            *a = AS_STRING(peek(1));
  int length = a->length + b->length;
  char *chars = ALLOCATE(char, length +1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString *result = takeString(chars, length);
  pop(); // for GC
  pop();
  push(OBJ_VAL(OBJ_CAST(result)));
}

static void loadUpvalues(CallFrame *frame, ObjClosure *closure) {
  for (int i = 0; i < closure->upvalueCount; ++i) {
    Upvalue *upVlu = &closure->function->chunk.compiler->upvalues[i];
    uint8_t isLocal = upVlu->isLocal,
            index   = upVlu->index;
    if (isLocal) {
      closure->upvalues[i] =
        captureUpvalue(frame->slots + index);
    } else {
      closure->upvalues[i] = frame->closure->upvalues[index];
    }
  }
}

static InterpretResult run() {
  CallFrame *frame = &vm.frames[vm.frameCount -1];

#ifdef DEBUG_TRACE_EXECUTION
  printf("\n===== execution =====\n");
# define TRACE_PRINT_EXECUTION \
    printf("\n        "); \
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++) { \
      printf("[%s]", valueToString(*slot)->chars); \
    } \
    printf("\n"); \
    disassembleInstruction(&frame->closure->function->chunk, \
        (int)(frame->ip - frame->closure->function->chunk.code))

# define TRACE_MODULE_LOAD printf("==== load a module ====\n");
# define TRACE_MODULE_LOADED printf("==== finished loading a module\n");
#else
# define TRACE_PRINT_EXECUTION
# define TRACE_MODULE_LOAD
# define TRACE_MODULE_LOADED
#endif

#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() \
  (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_SHORT() \
          (frame->ip += 2, \
          (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_STRING()   AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
  do { \
    double b = AS_NUMBER(pop()); \
    double a = AS_NUMBER(pop()); \
    push(valueType(a op b)); \
  } while(false)

# define DBG_NEXT \
  if (debugger.state > DBG_RUN) onNextTick(instruction)

#ifdef COMPUTED_GOTO
# define OP(opcode)  &&lbl_##opcode
# define CASE(inst)   lbl_##inst:

  void* labels[] = {
    OP(OP_CONSTANT), OP(OP_NIL), OP(OP_TRUE), OP(OP_FALSE),
    OP(OP_POP), OP(OP_GET_LOCAL), OP(OP_GET_GLOBAL), OP(OP_GET_REFERENCE)
    OP(OP_GET_UPVALUE), OP(OP_GET_PROPERTY), OP(OP_GET_SUBSCRIPT),
    OP(OP_GET_SUPER), OP(OP_DEFINE_GLOBAL), OP(OP_SET_LOCAL), OP(OP_SET_REFERENCE)
    OP(OP_SET_GLOBAL), OP(OP_SET_UPVALUE), OP(OP_SET_PROPERTY),
    OP(OP_SET_SUBSCRIPT), OP(OP_EQUAL), OP(OP_GREATER), OP(OP_LESS),
    OP(OP_ADD), OP(OP_SUBTRACT), OP(OP_MULTIPLY), OP(OP_DIVIDE),
    OP(OP_NOT), OP(OP_NEGATE), OP(OP_PRINT), OP(OP_JUMP),
    OP(OP_JUMP_IF_FALSE), OP(OP_LOOP), OP(OP_CALL), OP(OP_INVOKE),
    OP(OP_SUPER_INVOKE), OP(OP_CLOSURE), OP(OP_CLOSE_UPVALUE),
    OP(OP_RETURN), OP(OP_EVAL_EXIT), OP(OP_CLASS), OP(OP_INHERIT),
    OP(OP_METHOD), OP(OP_DEFINE_DICT), OP(OP_DICT_FIELD),
    OP(OP_DEFINE_ARRAY), OP(OP_ARRAY_PUSH), OP(OP_IMPORT_MODULE),
    OP(OP_IMPORT_VARIABLE), OP(OP_EXPORT)
  };
  assert(sizeof(labels) / sizeof(labels[0])==_OP_END);
# define BREAK \
  TRACE_PRINT_EXECUTION; \
  /* for single step */ \
  if (debugger.state == DBG_STEP) onNextTick(instruction); \
  goto *labels[instruction = READ_BYTE()]
# define SWITCH(expr) goto *labels[instruction = READ_BYTE()];

#else

# define CASE(inst)        case inst:
# define BREAK             \
  /* for single step */ \
  if (debugger.state == DBG_STEP) onNextTick(instruction); \
  break
# define SWITCH(expr)   switch(expr)
#endif


  uint8_t instruction;
  ObjModule *importModule = NULL;
  loadUpvalues(frame, frame->closure);

  for(;;) {
    TRACE_PRINT_EXECUTION;
    SWITCH(instruction = READ_BYTE()) {
    CASE(OP_CONSTANT) {
      Value constant = READ_CONSTANT();
      push(constant);
    } BREAK;
    CASE(OP_NIL)         push(NIL_VAL); BREAK;
    CASE(OP_TRUE)        push(BOOL_VAL(true)); BREAK;
    CASE(OP_FALSE)       push(BOOL_VAL(false)); BREAK;
    CASE(OP_POP)         pop(); BREAK;
    CASE(OP_GET_LOCAL) {
      uint8_t slot = READ_BYTE();
      push(frame->slots[slot]);
    } BREAK;
    CASE(OP_GET_REFERENCE) {
      uint8_t slot = READ_BYTE();
      assert(IS_REFERENCE(frame->slots[slot]));
      push(refGet(AS_REFERENCE(frame->slots[slot])));
    } BREAK;
    CASE(OP_GET_GLOBAL) {
      ObjString *name = READ_STRING();
      Value value;
      if (!tableGet(&vm.globals, name, &value))
        return runtimeError("Undefined variable '%s'.", name->chars);

      push(value);
    } BREAK;
    CASE(OP_GET_UPVALUE) {
      uint8_t slot = READ_BYTE();
      push(*frame->closure->upvalues[slot]->location);
    } BREAK;
    CASE(OP_GET_PROPERTY) {
      Table *tbl = NULL;
      Value obj = pop();
      ObjString *name = READ_STRING();
      if (IS_DICT(obj)) {
        tbl = &AS_DICT(obj)->fields;
      } else if (IS_INSTANCE(obj)) {
        tbl = &AS_INSTANCE(obj)->fields;
      }

      Value value = NIL_VAL;
      if (tbl && tableGet(tbl, name, &value)) {
        push(value);
        if (IS_INSTANCE(obj) &&
            !bindMethod(AS_INSTANCE(obj)->klass, name))
        {
          return INTERPRET_RUNTIME_ERROR;
        }
      } else {
        Value prop = objPropNative(AS_OBJ(obj), name);
        if (!IS_NIL(prop) && AS_NATIVE_PROP(prop)->getFn)
          value = AS_NATIVE_PROP(prop)->getFn(obj);
        else
          value = NIL_VAL;
        push(value);
      }

    } BREAK;
    CASE(OP_GET_INDEXER) {
      Value key = pop(), obj = pop();
      Value method = objMethodNative(AS_OBJ(obj), copyString("__getitem__", 11));
      if (!IS_NIL(method)) {
        push(AS_NATIVE_METHOD(method)->method(obj, 1, &key));
      } else {
        return runtimeError("Object can't use indexer [].\n");
      }
    } BREAK;
    CASE(OP_GET_SUPER) {
      ObjString *name = READ_STRING();
      ObjClass *superClass = AS_CLASS(pop());

      if (!bindMethod(superClass, name)) {
        return INTERPRET_RUNTIME_ERROR;
      }
    } BREAK;
    CASE(OP_DEFINE_GLOBAL) {
      ObjString *name = READ_STRING();
      tableSet(&vm.globals, name, peek(0));
      pop();
      DBG_NEXT;
    } BREAK;
    CASE(OP_SET_LOCAL) {
      uint8_t slot = READ_BYTE();
      frame->slots[slot] = peek(0);
      DBG_NEXT;
    } BREAK;
    CASE(OP_SET_REFERENCE) {
      uint8_t slot = READ_BYTE();
      assert(IS_REFERENCE(frame->slots[slot]));
      refSet(AS_REFERENCE(frame->slots[slot]), peek(0));
      DBG_NEXT;
    } BREAK;
    CASE(OP_SET_GLOBAL) {
      ObjString *name = READ_STRING();
      if (tableSet(&vm.globals, name, peek(0))) {
        tableDelete(&vm.globals, name);
        return runtimeError("Undefined variable '%s'.", name->chars);
      }
      DBG_NEXT;
    } BREAK;
    CASE(OP_SET_UPVALUE) {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = peek(0);
      DBG_NEXT;
    } BREAK;
    CASE(OP_SET_PROPERTY) {
      Table *tbl = NULL;
      Value value = pop(), obj = pop();
      ObjString *name = READ_STRING();
      if (IS_DICT(obj)) {
        tbl = &AS_DICT(obj)->fields;
      } else if (IS_INSTANCE(obj)) {
        tbl = &AS_INSTANCE(obj)->fields;
      }
      if (tbl && !tableHasKey(tbl, name)) {
        // lookup in prototype chain
        Value prop = objPropNative(AS_OBJ(obj), name);
        if (!IS_NIL(prop) && AS_NATIVE_PROP(prop)->setFn) {
          AS_NATIVE_PROP(prop)->setFn(obj, &value);
        } else
          tableSet(tbl, name, value);
        push(value);
      } else
        return runtimeError("Could not set '%s' to object.\n", name->chars);

      DBG_NEXT;
    } BREAK;
    CASE(OP_SET_INDEXER) {
      Value value = pop(), key = pop(), obj = pop();
      Value method = objMethodNative(AS_OBJ(obj), copyString("__setitem__", 11));
      if (!IS_NIL(method)) {
        Value args[] = {key, value};
        push(AS_NATIVE_METHOD(method)->method(obj, 2, args));
      } else {
        return runtimeError("Object can't use indexer [].\n");
      }
    } BREAK;
    CASE(OP_EQUAL) {
      Value b = pop(), a = pop();
      push(BOOL_VAL(valuesEqual(a, b)));
      DBG_NEXT;
    } BREAK;
    CASE(OP_GREATER)     BINARY_OP(BOOL_VAL, >); DBG_NEXT; BREAK;
    CASE(OP_LESS)        BINARY_OP(BOOL_VAL, <); DBG_NEXT; BREAK;
    CASE(OP_ADD) {
      if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
        concatenate();
      } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
        BINARY_OP(NUMBER_VAL, +);
      } else {
        return runtimeError("Operands must be two numbers or two strings.");
      }
      DBG_NEXT;
    } BREAK;
    CASE(OP_SUBTRACT)    BINARY_OP(NUMBER_VAL, -); DBG_NEXT; BREAK;
    CASE(OP_MULTIPLY)    BINARY_OP(NUMBER_VAL, *); DBG_NEXT; BREAK;
    CASE(OP_DIVIDE)      BINARY_OP(NUMBER_VAL, /); DBG_NEXT; BREAK;
    CASE(OP_NOT)
      push(BOOL_VAL(isFalsey(pop()))); BREAK;
    CASE(OP_NEGATE)
      if (!IS_NUMBER(peek(0))) {
        return runtimeError("Operand must be a number.");
      }
      push(NUMBER_VAL(-AS_NUMBER(pop())));
      BREAK;
    CASE(OP_PRINT) {
      ObjString *vlu = valueToString(pop());
      // dont use printf as a \0 in string should NOT terminate output
      const char *c = vlu->chars, *end = c + vlu->length;
      while(c < end) putc(*c++, stdout);
      DBG_NEXT;
    } BREAK;
    CASE(OP_JUMP) {
      uint16_t offset = READ_SHORT();
      frame->ip += offset;
      DBG_NEXT;
    } BREAK;
    CASE(OP_JUMP_IF_FALSE) {
      uint16_t offset = READ_SHORT();
      if (isFalsey(peek(0)))
        frame->ip += offset;
      DBG_NEXT;
    } BREAK;
    CASE(OP_LOOP) {
      uint16_t offset = READ_SHORT();
      frame->ip -= offset;
      DBG_NEXT;
    } BREAK;
    CASE(OP_CALL) {
      int argCount = READ_BYTE();
      if (!callValue(peek(argCount), argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }

      if (vm.frameCount == FRAMES_MAX) {
        return runtimeError("Stack overflow.");
      }

      frame = &vm.frames[vm.frameCount -1];
    } BREAK;
    CASE(OP_INVOKE) {
      ObjString *method = READ_STRING();
      int argCount = READ_BYTE();
      if (!invoke(method, argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount -1];
      BREAK;
    }
    CASE(OP_SUPER_INVOKE) {
      ObjString *method = READ_STRING();
      int argCount = READ_BYTE();
      ObjClass *superClass = AS_CLASS(pop());
      if (!invokeFromClass(superClass, method, argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount -1];
    } BREAK;
    CASE(OP_CLOSURE) {
      ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
      ObjClosure *closure = newClosure(function);
      push(OBJ_VAL(OBJ_CAST(closure)));
      loadUpvalues(frame, closure);
      frame->ip += 2 * closure->upvalueCount;
    } BREAK;
    CASE(OP_CLOSE_UPVALUE)
      closeUpvalues(vm.stackTop - 1);
      pop(); BREAK;
    CASE(OP_RETURN) {
      Value result = pop();
      closeUpvalues(frame->slots);
      vm.frameCount--;
      vm.stackTop = frame->slots;
      if (vm.frameCount == vm.exitAtFrame) {
        // exit interpreter or the module imported
        return INTERPRET_OK;
      }

      push(result);
      DBG_NEXT;
      frame = &vm.frames[vm.frameCount -1];
    } BREAK;
    CASE(OP_EVAL_EXIT) {
      vm.frameCount--;
      return INTERPRET_OK;
    } BREAK;
    CASE(OP_CLASS)
      push(OBJ_VAL(OBJ_CAST(newClass(READ_STRING()))));
      DBG_NEXT;
      BREAK;
    CASE(OP_INHERIT) {
      Value superClass = peek(1);
      if (!IS_CLASS(superClass))
        return runtimeError("Superclass must be a class.");

      ObjClass* subClass = AS_CLASS(peek(0));
      tableAddAll(&AS_CLASS(superClass)->methods,
                  &subClass->methods);
      pop(); // subclass;
      DBG_NEXT;
    } BREAK;
    CASE(OP_METHOD)
      defineMethod(READ_STRING());
      DBG_NEXT;
      BREAK;
    CASE(OP_DEFINE_DICT)
      push(OBJ_VAL(OBJ_CAST(newDict())));
      DBG_NEXT;
      BREAK;
    CASE(OP_DICT_FIELD) {
      Table *fields = &AS_DICT(peek(1))->fields;
      tableSet(fields, READ_STRING(), pop());
      DBG_NEXT;
    } BREAK;
    CASE(OP_DEFINE_ARRAY)
      push(OBJ_VAL(OBJ_CAST(newArray())));
      DBG_NEXT;
      BREAK;
    CASE(OP_ARRAY_PUSH) {
      ValueArray *array = &AS_ARRAY(peek(1))->arr;
      pushValueArray(array, pop());
      DBG_NEXT;
    } BREAK;
    CASE(OP_IMPORT_MODULE) {
      TRACE_MODULE_LOAD
      Value path = READ_CONSTANT();
      assert(IS_STRING(path));
      importModule = AS_MODULE(getModuleByPath(path));
      TRACE_MODULE_LOADED
      if (importModule == NULL)
        return runtimeError("Failed to load script from: %s\n", AS_CSTRING(path));
      DBG_NEXT;
    } BREAK;
    CASE(OP_IMPORT_VARIABLE) {
      ObjString *nameInExport = AS_STRING(READ_CONSTANT()),
                *alias = AS_STRING(READ_CONSTANT());
      uint8_t   varIdx  = READ_BYTE();
      Value ref;
      if (!tableGet(&importModule->module->exports, nameInExport, &ref)) {
        return runtimeError("%s is not exported from %s as %s.\n",
                            nameInExport->chars,
                            importModule->module->name->chars,
                            alias->chars);
      }
      frame->slots[varIdx] = ref;
      vm.stackTop++;
    } BREAK;
    CASE(OP_EXPORT) {
      ObjString *ident = AS_STRING(READ_CONSTANT());
      uint8_t localIdx = READ_BYTE(),
              upIdx    = READ_BYTE();
      frame->closure->upvalues[upIdx] = captureUpvalue(&frame->slots[localIdx]);
      Value ref;
      if (tableGet(&frame->closure->function->chunk.module->exports,
                   ident, &ref))
      {
        AS_REFERENCE(ref)->closure = frame->closure;
      }
    }
    }
  }
#undef READ_BYTE
#undef READ_CONTANT
#undef READ_SHORT
#undef READ_STRING
#undef BINARY_OP
#undef CASE
}

// -------------------------------------------------------

void initVM() {
  initTable(&vm.strings);
  initTable(&vm.globals);
  resetStack();
  vm.infantObjects = vm.olderObjects = NULL;
  vm.infantBytesAllocated = 0;
  vm.olderBytesAllocated = 0;
  vm.infantNextGC = INFANT_GC_MIN;
  vm.olderNextGC = OLDER_GC_MIN;
  vm.frameCount = vm.exitAtFrame = 0;
  vm.modules = NULL;

  vm.initString = NULL;
  vm.initString = copyString("init", 4);
  initTypes();
  initDebugger();

  defineBuiltins();
}

void freeVM() {
  vm.initString = NULL;

  while(vm.modules != NULL) {
    Module *freeMod = vm.modules;
    vm.modules = freeMod->next;
    freeModule(freeMod);
    FREE(Module, freeMod);
  }

  freeTable(&vm.strings);
  freeTable(&vm.globals);
  freeObjectsModule();
  freeTypes();

  freeObjects();
}

InterpretResult interpretVM(Module *module) {
  call(module->closure, 0);
  runInitCommands(); // debugger debug breakpoint file
  return run();
}

InterpretResult vm_evalBuild(ObjClosure **closure, const char *source) {
  bool enabled = setGCenabled(false);
  CallFrame *frame = &vm.frames[vm.frameCount-1];
  ObjFunction *function = compileEvalExpr(
    source, &frame->closure->function->chunk);
  if (function == NULL) {
    *closure = NULL;
    setGCenabled(enabled);
    return INTERPRET_COMPILE_ERROR;
  }

  // create closure and store upvalues
 *closure = newClosure(function);
  (*closure)->upvalueCount = function->upvalueCount;
  push(OBJ_VAL(OBJ_CAST(*closure)));
  loadUpvalues(frame, *closure);
  setGCenabled(enabled);
  return INTERPRET_OK;
}

InterpretResult vm_evalRun(Value *value, ObjClosure *closure) {
  if (AS_CLOSURE(peek(0)) != closure)
    push(OBJ_VAL(OBJ_CAST(closure)));
  failOnRuntimeErr = true;
  int saveFrameCnt = vm.frameCount;

  // turn of debugger during eval
  DebugStates oldDbgState = debugger.state;
  debugger.state = DBG_RUN;

  call(closure, 0);
  InterpretResult res = run();
  *value = (res == INTERPRET_OK) ? pop() : NIL_VAL;
  pop(); // function

  // restore state
  debugger.state = oldDbgState;
  failOnRuntimeErr = true;
  vm.frameCount = saveFrameCnt;
  return res;
}

InterpretResult vm_eval(Value *value, const char *source) {
  ObjClosure *closure;

  InterpretResult res = vm_evalBuild(&closure, source);
  if (res != INTERPRET_OK) return res;

  return vm_evalRun(value, closure);
}

void addModuleVM(Module *module) {
  module->next = vm.modules;
  vm.modules = module;
}

Module *getModule(const char *path) {
  Module *mod = vm.modules;
  while (mod != NULL) {
    if (memcmp(mod->path->chars, path, mod->path->length) == 0) {
      return mod;
    }
  }
  return NULL;
}

Module *getCurrentModule() {
  return vm.frames[vm.frameCount-1].closure->function->chunk.module;
}

void delModuleVM(Module *module) {
  if (vm.modules == NULL) return;

  Module **next = &vm.modules;
  do {
    if (*next == module) {
      *next = (*next)->next;
      freeModule(module);
      return;
    }
  } while ((*next = (*next)->next));
}

void markRootsVM(ObjFlags flags) {
  for (Value *slot = vm.stack; slot < vm.stackTop; ++slot) {
    markValue(*slot, flags);
  }

  for (int i = 0; i < vm.frameCount; ++i) {
    markObject(OBJ_CAST(vm.frames[i].closure), flags);
  }

  for (ObjUpvalue *upvalue = vm.openUpvalues;
       upvalue != NULL;
       upvalue = upvalue->next)
  {
    markObject(OBJ_CAST(upvalue), flags);
  }

  markObject(OBJ_CAST(vm.initString), flags);
  markTable(&vm.strings, flags);
  markTable(&vm.globals, flags);

  Module *mod = vm.modules;
  while (mod != NULL) {
    markRootsModule(mod, flags);
    mod = mod->next;
  }
}

void sweepVM(ObjFlags flags) {
  tableRemoveWhite(&vm.strings, flags);
  tableRemoveWhite(&vm.globals, flags);

  Module *mod = vm.modules;
  while (mod != NULL) {
    sweepModule(mod, flags);
    mod = mod->next;
  }
}

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
  assert(vm.stackTop <= vm.stack + STACK_MAX * sizeof(vm.stack[0]) && "Moved stackpointer above max");
}

Value pop() {
  assert(vm.stackTop >= vm.stack && "Moved stackpointer below zero.");
  vm.stackTop--;
  return *vm.stackTop;
}

Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}
