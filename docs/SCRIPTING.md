# Scripting

Game logic and generators are written in [Wren](https://wren.io) 0.4.0, a small class-based language. Scripts are
modules in the game data and run sandboxed inside the engine (`src/script/ScriptVm.*`).

The engine API (which classes and methods scripts can call) is still being designed. Until it exists, scripts can
compute and print, and the engine can call their methods.

## Running a script

```sh
gridfin --data mygame/ --script scripts/hello     # runs scripts/hello.wren, prints its output, exits
```

## Modules

`import "scripts/player" for Player` loads `scripts/player.wren` from the mounted data (the `.wren` is optional).
Names starting with `./` or `../` are relative to the importing module. Each module is loaded once. Wren's optional
`random` module is available; `meta` (compiling source at runtime) is not.

## Sandbox and budgets

- Scripts can't reach files, the network or the OS. Wren has no such modules, and the engine only exposes what it
  binds as foreign methods.
- **Step budget:** every call from the engine may take at most `ScriptLimits::stepsPerCall` steps (1,000,000 by
  default). A step is a loop iteration or a method call, the only ways code can run for long. A script over budget
  stops with `file:line: Script ran out of its execution budget`. Catching that error with `Fiber.try` doesn't help:
  the catching fiber fails on its next step.
- **Memory budget:** the VM's heap is limited to `ScriptLimits::memoryBytes` (8 MB by default). A script that grows
  past it stops on its next step. If the memory is still held afterwards (by module variables, say), the VM is
  stopped and must be reloaded.

Wren's interpreter has no hook for this, so the build applies a small patch (`cmake/PatchWren.cmake`) that calls the
engine on every loop back-edge and method call. The patch fails loudly if the Wren version changes.

## Errors

Compile and runtime errors are reported as `file:line: message`, with the calls that led there:

```
scripts/enemy.wren:12: Null does not implement 'position'.
    called from scripts/level.wren:40 (update(_))
```

## Embedding (C++)

```cpp
script::ScriptVm vm(data);
vm.bind("scripts/game", "Engine", "log(_)", /*isStatic=*/true,
        [](script::ForeignCall& call) { log::info("{}", call.string(0)); });
vm.runModule("scripts/game");
vm.call("scripts/game", "Game", "update(_)", std::vector<script::Value>{1.0 / 60.0});
```

Foreign methods are declared in Wren with `foreign static log(message)` and must be bound before the module loads.
Arguments and results are null, booleans, numbers or strings; a wrong argument type aborts the calling fiber with an
error.
