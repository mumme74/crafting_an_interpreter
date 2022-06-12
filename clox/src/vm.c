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


VM vm; // global

// --------------------------------------------------------------

static Value clockNative(int argCount, Value *args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}

static void runtimeError(const char *format, ...) {
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
}

static void defineNative(const char *name, NativeFn function, int arity) {
  ObjString *fnname = copyString(name, (int)strlen(name));
  push(OBJ_VAL(OBJ_CAST(fnname)));
  push(OBJ_VAL(OBJ_CAST(newNative(function, fnname, arity))));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

static Value peek(int distance) {
  return vm.stackTop[-1 - distance];
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
    case OBJ_NATIVE: {
      ObjNative *nativeObj = AS_NATIVE_OBJ(callee);
      if (nativeObj->arity != argCount) {
        runtimeError("%s requires %d arguments.", nativeObj->name, nativeObj->arity);
        return false;
      }
      Value result = nativeObj->function(argCount, vm.stackTop - argCount);
      vm.stackTop -= argCount +1;
      push(result);
      return true;
    }
    case OBJ_DICT:
    case OBJ_STRING: case OBJ_UPVALUE:
    case OBJ_INSTANCE: case OBJ_FUNCTION:
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
  if (!IS_INSTANCE(reciever)) {
    runtimeError("Only instances have methods.");
    return false;
  }

  ObjInstance *instance = AS_INSTANCE(reciever);

  Value value;
  if (tableGet(&instance->fields, name, &value)) {
    vm.stackTop[-argCount -1] = value;
    return callValue(value, argCount);
  }

  return invokeFromClass(instance->klass, name, argCount);
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


static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
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



static InterpretResult run() {
  CallFrame *frame = &vm.frames[vm.frameCount -1];

#ifdef DEBUG_TRACE_EXECUTION
  printf("\n===== execution =====\n");
# define TRACE_PRINT_EXECUTION \
    printf("        "); \
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++) { \
      printf("[%s]", valueToString(*slot)->chars); \
    } \
    printf("\n"); \
    disassembleInstruction(&frame->closure->function->chunk, \
        (int)(frame->ip - frame->closure->function->chunk.code))
#else
# define TRACE_PRINT_EXECUTION
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

#ifdef COMPUTED_GOTO
# define OP(opcode)  &&lbl_##opcode
# define CASE(inst)   lbl_##inst:

  void* labels[] = {
    OP(OP_CONSTANT), OP(OP_NIL), OP(OP_TRUE), OP(OP_FALSE),
    OP(OP_POP), OP(OP_GET_LOCAL), OP(OP_GET_GLOBAL),
    OP(OP_GET_UPVALUE), OP(OP_GET_PROPERTY), OP(OP_GET_SUPER),
    OP(OP_DEFINE_GLOBAL), OP(OP_SET_LOCAL), OP(OP_SET_GLOBAL),
    OP(OP_SET_UPVALUE), OP(OP_SET_PROPERTY), OP(OP_EQUAL),
    OP(OP_GREATER), OP(OP_LESS), OP(OP_ADD), OP(OP_SUBTRACT),
    OP(OP_MULTIPLY), OP(OP_DIVIDE), OP(OP_NOT), OP(OP_NEGATE),
    OP(OP_PRINT), OP(OP_JUMP), OP(OP_JUMP_IF_FALSE), OP(OP_LOOP),
    OP(OP_CALL), OP(OP_INVOKE), OP(OP_SUPER_INVOKE), OP(OP_CLOSURE),
    OP(OP_CLOSE_UPVALUE), OP(OP_RETURN), OP(OP_CLASS),
    OP(OP_INHERIT), OP(OP_METHOD)
  };
# define BREAK \
  TRACE_PRINT_EXECUTION; \
  goto *labels[instruction = READ_BYTE()]
# define SWITCH(expr) goto *labels[instruction = READ_BYTE()];

#else

# define CASE(inst)        case inst:
# define BREAK             break
# define SWITCH(expr)   switch(expr)
#endif


  uint8_t instruction;

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
    CASE(OP_GET_GLOBAL) {
      ObjString *name = READ_STRING();
      Value value;
      if (!tableGet(&vm.globals, name, &value)) {
        runtimeError("Undefined variable '%s'.", name->chars);
        return INTERPRET_COMPILE_ERROR;
      }
      push(value);
    } BREAK;
    CASE(OP_GET_UPVALUE) {
      uint8_t slot = READ_BYTE();
      push(*frame->closure->upvalues[slot]->location);
    } BREAK;
    CASE(OP_GET_PROPERTY) {
      Table *tbl = NULL;
      Value obj = peek(0);
      if (IS_DICT(obj)) {
        tbl = &AS_DICT(obj)->fields;
      } else if (IS_INSTANCE(obj)) {
        tbl = &AS_INSTANCE(obj)->fields;
      } else {
        runtimeError("Only instances and dict's have fields.");
        return INTERPRET_RUNTIME_ERROR;
      }

      ObjString *name = READ_STRING();
      Value value;
      if (tableGet(tbl, name, &value)) {
        pop();
        push(value);
        BREAK;
      }

      if (IS_INSTANCE(obj) &&
          !bindMethod(AS_INSTANCE(obj)->klass, name))
      {
        return INTERPRET_RUNTIME_ERROR;
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
    } BREAK;
    CASE(OP_SET_LOCAL) {
      uint8_t slot = READ_BYTE();
      frame->slots[slot] = peek(0);
    } BREAK;
    CASE(OP_SET_GLOBAL) {
      ObjString *name = READ_STRING();
      if (tableSet(&vm.globals, name, peek(0))) {
        tableDelete(&vm.globals, name);
        runtimeError("Undefined variable '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }
    } BREAK;
    CASE(OP_SET_UPVALUE) {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = peek(0);
    } BREAK;
    CASE(OP_SET_PROPERTY) {
      Table *tbl = NULL;
      Value obj = peek(1);
      if (IS_DICT(obj)) {
        tbl = &AS_DICT(obj)->fields;
      } else if (IS_INSTANCE(obj)) {
        tbl = &AS_INSTANCE(obj)->fields;
      } else {
        runtimeError("Only instances and dict's have fields.");
        return INTERPRET_RUNTIME_ERROR;
      }

      tableSet(tbl, READ_STRING(), peek(0));
      Value value = pop();
      pop();
      push(value);
    } BREAK;
    CASE(OP_EQUAL) {
      Value b = pop(), a = pop();
      push(BOOL_VAL(valuesEqual(a, b)));
    } BREAK;
    CASE(OP_GREATER)     BINARY_OP(BOOL_VAL, >); BREAK;
    CASE(OP_LESS)        BINARY_OP(BOOL_VAL, <); BREAK;
    CASE(OP_ADD) {
      if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
        concatenate();
      } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
        BINARY_OP(NUMBER_VAL, +); BREAK;
      } else {
        runtimeError("Operands must be two numbers or two strings.");
        return INTERPRET_RUNTIME_ERROR;
      }
    } BREAK;
    CASE(OP_SUBTRACT)    BINARY_OP(NUMBER_VAL, -); BREAK;
    CASE(OP_MULTIPLY)    BINARY_OP(NUMBER_VAL, *); BREAK;
    CASE(OP_DIVIDE)      BINARY_OP(NUMBER_VAL, /); BREAK;
    CASE(OP_NOT)
      push(BOOL_VAL(isFalsey(pop()))); BREAK;
    CASE(OP_NEGATE)
      if (!IS_NUMBER(peek(0))) {
        runtimeError("Operand must be a number.");
        return INTERPRET_RUNTIME_ERROR;
      }
      push(NUMBER_VAL(-AS_NUMBER(pop())));
      BREAK;
    CASE(OP_PRINT) {
      ObjString *vlu = valueToString(pop());
      // dont use printf as a \0 in string should NOT terminate output
      const char *c = vlu->chars, *end = c + vlu->length;
      while(c < end) putc(*c++, stdout);
    } BREAK;
    CASE(OP_JUMP) {
      uint16_t offset = READ_SHORT();
      frame->ip += offset;
    } BREAK;
    CASE(OP_JUMP_IF_FALSE) {
      uint16_t offset = READ_SHORT();
      if (isFalsey(peek(0)))
        frame->ip += offset;
    } BREAK;
    CASE(OP_LOOP) {
      uint16_t offset = READ_SHORT();
      frame->ip -= offset;

    } BREAK;
    CASE(OP_CALL) {
      int argCount = READ_BYTE();
      if (!callValue(peek(argCount), argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }

      if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
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
      for (int i = 0; i < closure->upvalueCount; ++i) {
        uint8_t isLocal = READ_BYTE(),
                index   = READ_BYTE();
        if (isLocal) {
          closure->upvalues[i] =
            captureUpvalue(frame->slots + index);
        } else {
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
    } BREAK;
    CASE(OP_CLOSE_UPVALUE)
      closeUpvalues(vm.stackTop - 1);
      pop(); BREAK;
    CASE(OP_RETURN) {
      Value result = pop();
      closeUpvalues(frame->slots);
      vm.frameCount--;
      if (vm.frameCount == 0) {
        // exit interpreter
        pop();
        return INTERPRET_OK;
      }

      vm.stackTop = frame->slots;
      push(result);
      frame = &vm.frames[vm.frameCount -1];
    } BREAK;
    CASE(OP_CLASS)
      push(OBJ_VAL(OBJ_CAST(newClass(READ_STRING()))));
      BREAK;
    CASE(OP_INHERIT) {
      Value superClass = peek(1);
      if (!IS_CLASS(superClass)) {
        runtimeError("Superclass must be a class.");
        return INTERPRET_RUNTIME_ERROR;
      }

      ObjClass* subClass = AS_CLASS(peek(0));
      tableAddAll(&AS_CLASS(superClass)->methods,
                  &subClass->methods);
      pop(); // subclass;
    } BREAK;
    CASE(OP_METHOD)
      defineMethod(READ_STRING());
      BREAK;
    CASE(OP_DICT)
      push(OBJ_VAL(OBJ_CAST(newDict())));
      BREAK;
    CASE(OP_DICT_FIELD) {
      Table *fields = &AS_DICT(peek(1))->fields;
      tableSet(fields, READ_STRING(), peek(0));
      pop();
    } BREAK;
    }
  }
#undef READ_BYTE
#undef READ_CONTANT
#undef READ_SHORT
#undef READ_STRING
#undef BINARY_OP
#undef CASE
}


static void defineBuiltins() {
  // all builtin functions
  defineNative("clock", clockNative, 0);
  /*defineNative("str", toString, 1);
  defineNative("num", toNumber, 1);*/
}

// -------------------------------------------------------


void initVM() {
  resetStack();
  vm.infantObjects = vm.olderObjects = NULL;
  vm.infantBytesAllocated = 0;
  vm.olderBytesAllocated = 0;
  vm.infantNextGC = INFANT_GC_MIN;
  vm.olderNextGC = OLDER_GC_MIN;
  vm.modules = NULL;

  initTable(&vm.strings);
  initTable(&vm.globals);

  vm.initString = NULL;
  vm.initString = copyString("init", 4);

  defineBuiltins();
}

void freeVM() {
  freeTable(&vm.strings);
  freeTable(&vm.globals);
  vm.initString = NULL;

  while(vm.modules != NULL) {
    Module *freeMod = vm.modules;
    vm.modules = freeMod->next;
    freeModule(freeMod);
    FREE(Module, freeMod);
  }

  freeObjects();
}

/*InterpretResult interpret(const char *source) {
  setGCenabled(false);
  ObjFunction *function = compile(source);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;
  //function->module->chars = module;
  //function->module->length = strlen(module)

  //push(OBJ_VAL(OBJ_CAST(function)));
  ObjClosure *closure = newClosure(function);
  //pop();
  push(OBJ_VAL(OBJ_CAST(closure)));
  setGCenabled(true);

  call(closure, 0);

  return run();
}*/

InterpretResult interpretVM(Module *module) {
  call(module->closure, 0);
  return run();
}

void addModuleVM(Module *module) {
  module->next = vm.modules;
  vm.modules = module;
}

void delModuleVM(Module *module) {
  if (vm.modules == NULL) return;

  Module **next = &vm.modules;
  do {
    if (*next == module) {
      *next = (*next)->next;
      FREE(Module, module);
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
  markTable(&vm.globals, flags);

  Module *mod = vm.modules;
  while (mod != NULL) {
    markRootsModule(mod, flags);
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
