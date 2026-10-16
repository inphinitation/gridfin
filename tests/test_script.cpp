#include "script/ScriptVm.hpp"

#include <doctest/doctest.h>

#include <memory>

using namespace gridfin;
using namespace gridfin::script;

namespace {

/// A VM over an in-memory data source, collecting printed lines.
struct Fixture {
    data::Vfs vfs;
    data::MemorySource* files = nullptr;
    std::vector<std::string> printed;

    Fixture()
    {
        auto source = std::make_unique<data::MemorySource>();
        files = source.get();
        vfs.mount(std::move(source));
    }

    std::unique_ptr<ScriptVm> vm(ScriptLimits limits = {})
    {
        auto result = std::make_unique<ScriptVm>(vfs, limits);
        result->onPrint = [this](std::string_view line) { printed.emplace_back(line); };
        return result;
    }
};

std::string allErrors(const ScriptVm& vm)
{
    std::string text;
    for (const std::string& error : vm.errors()) text += error + "\n";
    return text;
}

} // namespace

TEST_CASE("Scripts import modules from the game data, relative or from the root")
{
    Fixture f;
    f.files->add("scripts/main.wren", "import \"./util/math\" for Twice\n"
                                      "import \"scripts/names\" for Name\n"
                                      "System.print(\"%(Name.hero) %(Twice.of(21))\")\n");
    f.files->add("scripts/util/math.wren", "import \"../names\" for Name\n"
                                           "class Twice {\n  static of(x) { x * 2 }\n}\n");
    f.files->add("scripts/names.wren", "class Name {\n  static hero { \"Gridfin\" }\n}\n");
    auto vm = f.vm();
    CHECK_MESSAGE(vm->runModule("scripts/main"), allErrors(*vm));
    CHECK(f.printed == std::vector<std::string>{"Gridfin 42"});

    // Wren's optional random module is available; the file system is not.
    CHECK(vm->runSource("probe", "import \"random\" for Random\nSystem.print(Random.new(1).int(10) >= 0)\n"));
    CHECK_FALSE(vm->runSource("escape", "import \"../outside\" for X\n"));
    CHECK_FALSE(vm->runSource("missing", "import \"scripts/nothere\" for X\n"));
}

TEST_CASE("The engine calls into scripts and scripts call foreign methods")
{
    Fixture f;
    f.files->add("game.wren", "class Engine {\n"
                              "  foreign static add(a, b)\n"
                              "  foreign static greet(name)\n"
                              "}\n"
                              "class Game {\n"
                              "  static update(dt) { Engine.add(dt, 1) }\n"
                              "  static hello(name) { Engine.greet(name) + \"!\" }\n"
                              "  static bad { Engine.add(\"x\", 1) }\n"
                              "}\n");
    auto vm = f.vm();
    vm->bind("game", "Engine", "add(_,_)", true, [](ForeignCall& call) {
        const double a = call.number(0), b = call.number(1);
        call.returnValue(a + b);
    });
    vm->bind("game", "Engine", "greet(_)", true,
             [](ForeignCall& call) { call.returnValue("hello " + call.string(0)); });
    REQUIRE_MESSAGE(vm->runModule("game"), allErrors(*vm));

    const std::vector<Value> dt{0.5};
    CHECK(vm->call("game", "Game", "update(_)", dt) == Value{1.5});
    const std::vector<Value> name{std::string("Saturn")};
    CHECK(vm->call("game", "Game", "hello(_)", name) == Value{std::string("hello Saturn!")});

    CHECK_FALSE(vm->call("game", "Game", "bad"));
    REQUIRE_FALSE(vm->errors().empty());
    CHECK(vm->errors().back().starts_with("game.wren:8: Argument 1 must be a number."));

    CHECK_FALSE(vm->call("game", "Nobody", "update(_)", dt));
    CHECK_FALSE(vm->call("game", "Game", "missing()"));
}

TEST_CASE("Errors point at the file and line")
{
    Fixture f;
    f.files->add("broken.wren", "var a = 1\nvar b = (\n");
    f.files->add("crash.wren", "class C {\n  static run() { inner() }\n  static inner() { null.oops }\n}\nC.run()\n");
    auto vm = f.vm();
    CHECK_FALSE(vm->runModule("broken"));
    REQUIRE_FALSE(vm->errors().empty());
    CHECK(vm->errors().front() == "broken.wren:2: Error at newline: Expected expression.");

    vm->clearErrors();
    CHECK_FALSE(vm->runModule("crash"));
    REQUIRE(vm->errors().size() == 1);
    const std::string& error = vm->errors().front();
    CHECK(error.starts_with("crash.wren:3: Null does not implement 'oops'."));
    CHECK(error.find("called from crash.wren:2 (run())") != std::string::npos);
}

TEST_CASE("A runaway script stops at its step budget, even if it catches the error")
{
    Fixture f;
    ScriptLimits limits;
    limits.stepsPerCall = 10'000;
    auto vm = f.vm(limits);

    CHECK_FALSE(vm->runSource("loop", "while (true) {}\n"));
    REQUIRE_FALSE(vm->errors().empty());
    CHECK(vm->errors().back().find("execution budget (10000 steps per call)") != std::string::npos);

    // Catching the error doesn't buy more time: the catching fiber fails on its next step.
    CHECK_FALSE(vm->runSource("sneaky", "var f = Fiber.new { while (true) {} }\n"
                                        "while (true) { f.try() }\n"));
    CHECK_FALSE(vm->runSource("recurse", "class R {\n  static go(n) { go(n + 1) }\n}\nR.go(0)\n"));

    // The budget is per call: the VM keeps working afterwards.
    CHECK(vm->runSource("fine", "var sum = 0\nfor (i in 1..100) sum = sum + i\nSystem.print(sum)\n"));
    CHECK(f.printed.back() == "5050");
    CHECK(vm->stats().steps < 1'000);
    CHECK(vm->alive());
}

TEST_CASE("A script can't allocate past its memory budget")
{
    Fixture f;
    ScriptLimits limits;
    limits.memoryBytes = 2 * 1024 * 1024;
    auto vm = f.vm(limits);
    // A local: garbage once the fiber dies.
    CHECK_FALSE(vm->runSource(
        "grow", "class G {\n  static run() {\n    var s = \"x\"\n    while (true) s = s + s\n  }\n}\nG.run()\n"));
    REQUIRE_FALSE(vm->errors().empty());
    CHECK(vm->errors().front().find("memory budget") != std::string::npos);
    CHECK(vm->stats().peakMemoryBytes < 3 * limits.memoryBytes);
    // The string was garbage once its fiber died, so the VM lives on.
    CHECK(vm->alive());
    CHECK(vm->runSource("after", "System.print(\"ok\")\n"));

    // Module variables keep their memory after the call: holding more than the budget stops the VM.
    // (Growing step by step stops at the budget, so it takes one big allocation to get past it.)
    CHECK_FALSE(vm->runSource("hold", "var chunk = \"x\"\nfor (i in 1..20) chunk = chunk + chunk\n"
                                      "var big = chunk + chunk\n"));
    CHECK_FALSE(vm->alive());
    CHECK(vm->errors().back().find("was stopped") != std::string::npos);
    CHECK_FALSE(vm->runSource("still", "System.print(1)\n"));
}
