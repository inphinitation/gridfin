#pragma once

#include "data/Vfs.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct WrenVM;
struct WrenHandle;

/// Wren scripting for game data.
///
/// Scripts are modules in the mounted game data: `import "scripts/player" for Player` loads
/// `scripts/player.wren`; `./` and `../` import relative to the importing module. Scripts are
/// sandboxed: they can't reach the file system or the OS (Wren has no such modules), only what the
/// engine binds as foreign methods. Every call from the engine runs on a budget of steps (loop
/// iterations and method calls) and memory, so a runaway script fails with an error instead of
/// hanging the game.
namespace gridfin::script {

struct ScriptLimits {
    std::uint64_t stepsPerCall = 1'000'000;    ///< loop iterations + method calls per engine call
    std::size_t memoryBytes = 8 * 1024 * 1024; ///< the VM's heap
};

/// A value passed between C++ and Wren. Other Wren objects arrive as null.
using Value = std::variant<std::monostate, bool, double, std::string>;

/// The arguments and result of a foreign method call. Argument indices start at 0 (the receiver,
/// `this` or the class, is not counted).
class ForeignCall {
public:
    explicit ForeignCall(WrenVM* vm) : m_vm(vm) {}

    [[nodiscard]] int count() const;
    [[nodiscard]] double number(int index);
    [[nodiscard]] std::string string(int index);
    [[nodiscard]] bool boolean(int index);
    [[nodiscard]] Value value(int index) const;

    void returnValue(const Value& value);
    /// Aborts the calling fiber with a runtime error.
    void fail(std::string message);

    [[nodiscard]] bool failed() const noexcept { return m_error.has_value(); }
    [[nodiscard]] const std::optional<std::string>& error() const noexcept { return m_error; }

private:
    WrenVM* m_vm;
    std::optional<std::string> m_error;
};

using ForeignMethod = std::function<void(ForeignCall&)>;

struct ScriptStats {
    std::uint64_t steps = 0;     ///< steps used by the last engine call
    std::size_t memoryBytes = 0; ///< current heap size
    std::size_t peakMemoryBytes = 0;
};

class ScriptVm {
public:
    explicit ScriptVm(const data::Vfs& data, ScriptLimits limits = {});
    ~ScriptVm();
    ScriptVm(const ScriptVm&) = delete;
    ScriptVm& operator=(const ScriptVm&) = delete;

    /// Makes a C++ function available as a `foreign` method. Bind before loading the module that
    /// declares it. `signature` is Wren's: "name(_,_)", "name", "name=(_)", "[_]".
    void bind(std::string_view module, std::string_view className, std::string_view signature, bool isStatic,
              ForeignMethod method);

    /// Loads and runs a module from the game data ("scripts/main" or "scripts/main.wren").
    bool runModule(std::string_view module);
    /// Runs source text as a module (for tests and tools).
    bool runSource(std::string_view module, std::string_view source);

    /// Calls a method on a top-level variable (usually a class, for static methods):
    /// call("scripts/game", "Game", "update(_)", {0.016}). Returns nullopt on error.
    std::optional<Value> call(std::string_view module, std::string_view variable, std::string_view signature,
                              std::span<const Value> arguments = {});

    /// False once the VM is unusable (its memory stayed over budget). Reload it.
    [[nodiscard]] bool alive() const noexcept { return m_alive; }
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept { return m_errors; }
    void clearErrors() { m_errors.clear(); }
    [[nodiscard]] const ScriptStats& stats() const noexcept { return m_stats; }
    [[nodiscard]] const ScriptLimits& limits() const noexcept { return m_limits; }

    /// Where System.print goes (one call per line). Defaults to the log.
    std::function<void(std::string_view)> onPrint;

    // Called from Wren's C callbacks.
    bool step();
    void* reallocate(void* memory, std::size_t newSize);
    void write(std::string_view text);
    void reportError(int type, const char* module, int line, const char* message);
    [[nodiscard]] std::optional<std::string> resolveModule(std::string_view importer, std::string_view name) const;
    [[nodiscard]] std::optional<std::string> loadModule(std::string_view name) const;
    [[nodiscard]] int findForeign(std::string_view module, std::string_view className, bool isStatic,
                                  std::string_view signature) const;
    void invokeForeign(int index, WrenVM* vm);

private:
    bool finish(int result);
    void checkMemory();

    const data::Vfs& m_data;
    ScriptLimits m_limits;
    WrenVM* m_vm = nullptr;
    std::map<std::string, WrenHandle*, std::less<>> m_callHandles;
    std::map<std::string, int, std::less<>> m_foreignIndex;
    std::vector<ForeignMethod> m_foreign;

    std::vector<std::string> m_errors;
    std::string m_pendingError; ///< a runtime error, collecting its stack trace
    int m_pendingFrames = 0;
    std::string m_printBuffer;
    ScriptStats m_stats;
    enum class Budget { Ok, Steps, Memory } m_budget = Budget::Ok;
    bool m_alive = true;
};

} // namespace gridfin::script
