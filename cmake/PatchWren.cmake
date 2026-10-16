# Adds Gridfin's execution budget hook to Wren's interpreter (run as a FetchContent patch step).
#
# Wren has no instruction hook, so a function pointer is called on every loop back-edge and every
# method call: the only ways a script can run for long. When it returns false, the fiber aborts with
# a runtime error. Fibers that catch that error hit it again on their next loop or call, so a script
# can't escape the budget.
set(file "src/vm/wren_vm.c")
file(READ "${file}" source)
if(source MATCHES "wrenGridfinStepHook")
    return() # already patched
endif()

macro(patch_once before after)
    string(FIND "${source}" "${before}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "PatchWren: '${before}' not found in ${file}; the Wren version changed")
    endif()
    string(REPLACE "${before}" "${after}" source "${source}")
endmacro()

set(check "      if (wrenGridfinStepHook != NULL && !wrenGridfinStepHook(vm))
      {
        vm->fiber->error = CONST_STRING(vm, \"Script ran out of its execution budget.\");
        RUNTIME_ERROR();
      }
")

patch_once("// The behavior of realloc() when the size is 0"
"// Gridfin: called on loop back-edges and method calls; returning false aborts the fiber.
bool (*wrenGridfinStepHook)(WrenVM* vm) = NULL;

// The behavior of realloc() when the size is 0")

# Before the jump, so an error reports the line of the loop's end rather than the code before the loop.
patch_once("      ip -= offset;
      DISPATCH();"
"${check}      ip -= offset;
      DISPATCH();")

patch_once("    completeCall:
"
"    completeCall:
${check}")

file(WRITE "${file}" "${source}")
