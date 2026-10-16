#include "script/ScriptVm.hpp"

#include "core/Log.hpp"

#include <wren.hpp>

#include <array>
#include <cstdlib>
#include <cstring>
#include <format>
#include <utility>

// Added to Wren's interpreter by cmake/PatchWren.cmake: called on every loop back-edge and method
// call; returning false aborts the running fiber.
extern "C" bool (*wrenGridfinStepHook)(WrenVM* vm);

namespace gridfin::script {

namespace {

constexpr std::size_t kMaxForeignMethods = 256;
constexpr std::size_t kAllocationHeader = 16; // keeps allocations 16-byte aligned

ScriptVm& self(WrenVM* vm)
{
    return *static_cast<ScriptVm*>(wrenGetUserData(vm));
}

// Wren's foreign methods are plain function pointers with no user data, so each bound method
// gets its own trampoline that knows its index.
template <std::size_t Index>
void trampoline(WrenVM* vm)
{
    self(vm).invokeForeign(static_cast<int>(Index), vm);
}

template <std::size_t... Indices>
constexpr std::array<WrenForeignMethodFn, sizeof...(Indices)> makeTrampolines(std::index_sequence<Indices...>)
{
    return {&trampoline<Indices>...};
}

constexpr auto kTrampolines = makeTrampolines(std::make_index_sequence<kMaxForeignMethods>{});

std::string foreignKey(std::string_view module, std::string_view className, bool isStatic, std::string_view signature)
{
    return std::format("{}|{}|{}{}", module, className, isStatic ? "static " : "", signature);
}

/// Copies a string into memory Wren may free (it frees resolved module names with its allocator).
char* wrenString(WrenVM* vm, const std::string& text)
{
    auto* copy = static_cast<char*>(self(vm).reallocate(nullptr, text.size() + 1));
    std::memcpy(copy, text.c_str(), text.size() + 1);
    return copy;
}

std::string displayModule(const char* module)
{
    if (!module) return "?";
    const std::string_view name = module;
    if (name == "random") return "random";
    return std::format("{}.wren", name);
}

} // namespace

// --- ForeignCall --------------------------------------------------------------------------------

int ForeignCall::count() const
{
    return wrenGetSlotCount(m_vm) - 1;
}

Value ForeignCall::value(int index) const
{
    const int slot = index + 1;
    if (index < 0 || slot >= wrenGetSlotCount(m_vm)) return std::monostate{};
    switch (wrenGetSlotType(m_vm, slot)) {
    case WREN_TYPE_BOOL: return wrenGetSlotBool(m_vm, slot);
    case WREN_TYPE_NUM: return wrenGetSlotDouble(m_vm, slot);
    case WREN_TYPE_STRING: {
        int length = 0;
        const char* bytes = wrenGetSlotBytes(m_vm, slot, &length);
        return std::string(bytes, static_cast<std::size_t>(length));
    }
    default: return std::monostate{};
    }
}

double ForeignCall::number(int index)
{
    const Value v = value(index);
    if (const double* number = std::get_if<double>(&v)) return *number;
    fail(std::format("Argument {} must be a number.", index + 1));
    return 0.0;
}

std::string ForeignCall::string(int index)
{
    Value v = value(index);
    if (std::string* text = std::get_if<std::string>(&v)) return std::move(*text);
    fail(std::format("Argument {} must be a string.", index + 1));
    return {};
}

bool ForeignCall::boolean(int index)
{
    const Value v = value(index);
    if (const bool* flag = std::get_if<bool>(&v)) return *flag;
    fail(std::format("Argument {} must be a boolean.", index + 1));
    return false;
}

void ForeignCall::returnValue(const Value& value)
{
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                wrenSetSlotBool(m_vm, 0, v);
            } else if constexpr (std::is_same_v<T, double>) {
                wrenSetSlotDouble(m_vm, 0, v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                wrenSetSlotBytes(m_vm, 0, v.data(), v.size());
            } else {
                wrenSetSlotNull(m_vm, 0);
            }
        },
        value);
}

void ForeignCall::fail(std::string message)
{
    if (!m_error) m_error = std::move(message); // the first problem is the useful one
}

// --- ScriptVm -----------------------------------------------------------------------------------

ScriptVm::ScriptVm(const data::Vfs& data, ScriptLimits limits) : m_data(data), m_limits(limits)
{
    wrenGridfinStepHook = [](WrenVM* vm) { return self(vm).step(); };

    WrenConfiguration config;
    wrenInitConfiguration(&config);
    config.userData = this;
    config.reallocateFn = [](void* memory, std::size_t newSize, void* user) {
        return static_cast<ScriptVm*>(user)->reallocate(memory, newSize);
    };
    config.writeFn = [](WrenVM* vm, const char* text) { self(vm).write(text); };
    config.errorFn = [](WrenVM* vm, WrenErrorType type, const char* module, int line, const char* message) {
        self(vm).reportError(static_cast<int>(type), module, line, message);
    };
    config.resolveModuleFn = [](WrenVM* vm, const char* importer, const char* name) -> const char* {
        const auto resolved = self(vm).resolveModule(importer ? importer : "", name);
        return resolved ? wrenString(vm, *resolved) : nullptr;
    };
    config.loadModuleFn = [](WrenVM* vm, const char* name) {
        WrenLoadModuleResult result{};
        if (const auto source = self(vm).loadModule(name)) {
            auto* copy = new char[source->size() + 1];
            std::memcpy(copy, source->c_str(), source->size() + 1);
            result.source = copy;
            result.onComplete = [](WrenVM*, const char*, WrenLoadModuleResult loaded) { delete[] loaded.source; };
        }
        return result; // a null source lets Wren try its optional modules ("random")
    };
    config.bindForeignMethodFn = [](WrenVM* vm, const char* module, const char* className, bool isStatic,
                                    const char* signature) -> WrenForeignMethodFn {
        const int index = self(vm).findForeign(module, className, isStatic, signature);
        return index < 0 ? nullptr : kTrampolines[static_cast<std::size_t>(index)];
    };
    // Collect early and often: the budget is small.
    config.initialHeapSize = std::min<std::size_t>(1024 * 1024, m_limits.memoryBytes / 4);
    config.minHeapSize = 64 * 1024;
    config.heapGrowthPercent = 50;
    m_vm = wrenNewVM(&config);
}

ScriptVm::~ScriptVm()
{
    for (auto& [signature, handle] : m_callHandles) wrenReleaseHandle(m_vm, handle);
    wrenFreeVM(m_vm);
}

void ScriptVm::bind(std::string_view module, std::string_view className, std::string_view signature, bool isStatic,
                    ForeignMethod method)
{
    const std::string key = foreignKey(module, className, isStatic, signature);
    if (const auto it = m_foreignIndex.find(key); it != m_foreignIndex.end()) {
        m_foreign[static_cast<std::size_t>(it->second)] = std::move(method);
        return;
    }
    if (m_foreign.size() >= kMaxForeignMethods) {
        log::error("Too many foreign methods (at most {}); {} is not bound", kMaxForeignMethods, key);
        return;
    }
    m_foreignIndex.emplace(key, static_cast<int>(m_foreign.size()));
    m_foreign.push_back(std::move(method));
}

int ScriptVm::findForeign(std::string_view module, std::string_view className, bool isStatic,
                          std::string_view signature) const
{
    const auto it = m_foreignIndex.find(foreignKey(module, className, isStatic, signature));
    return it == m_foreignIndex.end() ? -1 : it->second;
}

void ScriptVm::invokeForeign(int index, WrenVM* vm)
{
    ForeignCall call(vm);
    m_foreign[static_cast<std::size_t>(index)](call);
    if (call.failed()) {
        wrenSetSlotString(vm, 0, call.error()->c_str());
        wrenAbortFiber(vm, 0);
    }
}

bool ScriptVm::step()
{
    if (++m_stats.steps > m_limits.stepsPerCall) {
        m_budget = Budget::Steps;
        return false;
    }
    if (m_stats.memoryBytes > m_limits.memoryBytes) {
        m_budget = Budget::Memory;
        return false;
    }
    return true;
}

void* ScriptVm::reallocate(void* memory, std::size_t newSize)
{
    // Every block carries its size in a header, so the heap size is known exactly.
    std::size_t oldSize = 0;
    std::byte* block = nullptr;
    if (memory) {
        block = static_cast<std::byte*>(memory) - kAllocationHeader;
        std::memcpy(&oldSize, block, sizeof(oldSize));
    }
    if (newSize == 0) {
        std::free(block);
        m_stats.memoryBytes -= oldSize;
        return nullptr;
    }
    auto* resized = static_cast<std::byte*>(std::realloc(block, newSize + kAllocationHeader));
    if (!resized) {
        log::error("Out of memory in the script VM");
        std::abort(); // Wren can't recover from a failed allocation
    }
    std::memcpy(resized, &newSize, sizeof(newSize));
    m_stats.memoryBytes = m_stats.memoryBytes - oldSize + newSize;
    m_stats.peakMemoryBytes = std::max(m_stats.peakMemoryBytes, m_stats.memoryBytes);
    return resized + kAllocationHeader;
}

void ScriptVm::write(std::string_view text)
{
    m_printBuffer += text;
    std::size_t newline = 0;
    while ((newline = m_printBuffer.find('\n')) != std::string::npos) {
        const std::string line = m_printBuffer.substr(0, newline);
        if (onPrint) {
            onPrint(line);
        } else {
            log::info("[script] {}", line);
        }
        m_printBuffer.erase(0, newline + 1);
    }
}

void ScriptVm::reportError(int type, const char* module, int line, const char* message)
{
    switch (static_cast<WrenErrorType>(type)) {
    case WREN_ERROR_COMPILE: m_errors.push_back(std::format("{}:{}: {}", displayModule(module), line, message)); break;
    case WREN_ERROR_RUNTIME: {
        std::string text = message;
        if (m_budget == Budget::Steps) {
            text = std::format("Script ran out of its execution budget ({} steps per call).", m_limits.stepsPerCall);
        } else if (m_budget == Budget::Memory) {
            text = std::format("Script used more than its memory budget ({} KB).", m_limits.memoryBytes / 1024);
        }
        m_pendingError = text;
        m_pendingFrames = 0;
        break;
    }
    case WREN_ERROR_STACK_TRACE:
        if (m_pendingFrames++ == 0) {
            // The innermost frame says where the error happened.
            m_pendingError = std::format("{}:{}: {}", displayModule(module), line, m_pendingError);
        } else {
            m_pendingError += std::format("\n    called from {}:{} ({})", displayModule(module), line, message);
        }
        break;
    }
}

std::optional<std::string> ScriptVm::resolveModule(std::string_view importer, std::string_view name) const
{
    if (name == "random") return std::string(name); // Wren's optional module
    std::string path(name);
    if (path.ends_with(".wren")) path.resize(path.size() - 5);
    if (path.starts_with("./") || path.starts_with("../")) {
        return data::resolveRelative(std::format("{}.wren", importer), path);
    }
    return data::normalizePath(path);
}

std::optional<std::string> ScriptVm::loadModule(std::string_view name) const
{
    return m_data.readText(std::format("{}.wren", name));
}

bool ScriptVm::finish(int result)
{
    if (!m_pendingError.empty()) {
        m_errors.push_back(std::move(m_pendingError));
        m_pendingError.clear();
    }
    m_budget = Budget::Ok;
    checkMemory();
    return result == WREN_RESULT_SUCCESS && m_alive;
}

void ScriptVm::checkMemory()
{
    if (m_stats.memoryBytes <= m_limits.memoryBytes) return;
    wrenCollectGarbage(m_vm);
    if (m_stats.memoryBytes > m_limits.memoryBytes) {
        m_alive = false;
        m_errors.push_back(std::format("The script VM holds {} KB, over its {} KB budget, and was stopped.",
                                       m_stats.memoryBytes / 1024, m_limits.memoryBytes / 1024));
    }
}

bool ScriptVm::runModule(std::string_view module)
{
    const auto name = resolveModule("", module);
    if (!name) {
        m_errors.push_back(std::format("Invalid module name '{}'.", module));
        return false;
    }
    const auto source = loadModule(*name);
    if (!source) {
        m_errors.push_back(std::format("{}.wren: not found", *name));
        return false;
    }
    return runSource(*name, *source);
}

bool ScriptVm::runSource(std::string_view module, std::string_view source)
{
    if (!m_alive) return false;
    m_stats.steps = 0;
    const std::string moduleName(module), text(source);
    return finish(wrenInterpret(m_vm, moduleName.c_str(), text.c_str()));
}

std::optional<Value> ScriptVm::call(std::string_view module, std::string_view variable, std::string_view signature,
                                    std::span<const Value> arguments)
{
    if (!m_alive) return std::nullopt;
    const std::string moduleName(module), variableName(variable);
    if (!wrenHasModule(m_vm, moduleName.c_str()) || !wrenHasVariable(m_vm, moduleName.c_str(), variableName.c_str())) {
        m_errors.push_back(std::format("{}.wren: no variable '{}' to call {} on", module, variable, signature));
        return std::nullopt;
    }

    auto handle = m_callHandles.find(signature);
    if (handle == m_callHandles.end()) {
        handle = m_callHandles.emplace(std::string(signature), wrenMakeCallHandle(m_vm, std::string(signature).c_str()))
                     .first;
    }

    wrenEnsureSlots(m_vm, static_cast<int>(arguments.size()) + 1);
    wrenGetVariable(m_vm, moduleName.c_str(), variableName.c_str(), 0);
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const int slot = static_cast<int>(i) + 1;
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>) {
                    wrenSetSlotBool(m_vm, slot, v);
                } else if constexpr (std::is_same_v<T, double>) {
                    wrenSetSlotDouble(m_vm, slot, v);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    wrenSetSlotBytes(m_vm, slot, v.data(), v.size());
                } else {
                    wrenSetSlotNull(m_vm, slot);
                }
            },
            arguments[i]);
    }

    m_stats.steps = 0;
    if (!finish(wrenCall(m_vm, handle->second))) return std::nullopt;
    switch (wrenGetSlotType(m_vm, 0)) {
    case WREN_TYPE_BOOL: return wrenGetSlotBool(m_vm, 0);
    case WREN_TYPE_NUM: return wrenGetSlotDouble(m_vm, 0);
    case WREN_TYPE_STRING: {
        int length = 0;
        const char* bytes = wrenGetSlotBytes(m_vm, 0, &length);
        return std::string(bytes, static_cast<std::size_t>(length));
    }
    default: return Value{};
    }
}

} // namespace gridfin::script
