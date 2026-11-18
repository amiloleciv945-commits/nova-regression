#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "nova/components/algorithm/gauss_seidel_algorithm.hpp"
#include "nova/components/structure/simulation_structure.hpp"

#include <cmath>
#include <limits>

using namespace nova;
using Catch::Matchers::WithinAbs;

namespace
{

const std::filesystem::path identityFmu = std::filesystem::path(DATA_FOLDER) / "fmus/1.0/identity.fmu";

class test_model : public model_instance
{
public:
    using model_instance::model_instance;

    void enter_initialization_mode(double) override { }
    void exit_initialization_mode() override { }
    void step(double, double) override { ++steps; }
    bool try_step(double time, double stepSize) override
    {
        if (rejectStep) return false;
        step(time, stepSize);
        return true;
    }
    void terminate() override { }
    void reset() override { steps = 0; }

    bool rejectStep = false;
    int steps = 0;
};

} // namespace

TEST_CASE("Gauss-Seidel propagates new outputs through three FMUs in one step", "[gauss_seidel]")
{
    constexpr double stepSize = 0.1; // Dahlquist's fixed internal Euler step.
    constexpr double startTime = 1.0;
    simulation_structure structure;
    structure.add_model("sink", identityFmu);
    structure.add_model("relay", identityFmu);
    structure.add_model("source", std::filesystem::path(DATA_FOLDER) / "fmus/3.0/ref/Dahlquist.fmu");
    structure.make_connection<double>("source::x", "relay::realIn", std::function<double(const double&)>(
        [](double x) { return 2.0 * x; }));
    structure.make_connection<double>("relay::realOut", "sink::realIn");

    auto sim = structure.load(std::make_unique<gauss_seidel_algorithm>(
        stepSize, std::vector<std::string>{"source", "relay", "sink"}));
    sim->init(startTime);
    REQUIRE(sim->time() == startTime);

    auto* source = sim->get_real_property("source::x");
    auto* relay = sim->get_real_property("relay::realOut");
    auto* sink = sim->get_real_property("sink::realOut");
    REQUIRE(source);
    REQUIRE(relay);
    REQUIRE(sink);
    CHECK_THAT(source->get_value(), WithinAbs(1.0, 1e-12));
    for (int i = 1; i <= 20; ++i) {
        const double t = sim->step();
        CHECK_THAT(t, WithinAbs(startTime + i * stepSize, 1e-12));
        const double x = source->get_value();
        CHECK_THAT(x, WithinAbs(std::pow(1.0 - stepSize, i), 1e-12));
        CHECK_THAT(relay->get_value(), WithinAbs(2.0 * x, 1e-12));
        CHECK_THAT(sink->get_value(), WithinAbs(2.0 * x, 1e-12));
    }
    sim->terminate();
}

TEST_CASE("Gauss-Seidel advances a three-FMU feedback loop once per communication step", "[gauss_seidel]")
{
    simulation_structure structure;
    for (const auto* name : {"c", "b", "a"}) {
        structure.add_model(name, identityFmu);
    }
    structure.make_connection<double>("a::realOut", "b::realIn", std::function<double(const double&)>(
        [](double x) { return x + 1.0; }));
    structure.make_connection<double>("b::realOut", "c::realIn", std::function<double(const double&)>(
        [](double x) { return 2.0 * x; }));
    structure.make_connection<double>("c::realOut", "a::realIn", std::function<double(const double&)>(
        [](double x) { return 0.25 * x; }));

    auto sim = structure.load(std::make_unique<gauss_seidel_algorithm>(
        0.01, std::vector<std::string>{"a", "b", "c"}));
    sim->init();
    auto* a = sim->get_real_property("a::realOut");
    auto* b = sim->get_real_property("b::realOut");
    auto* c = sim->get_real_property("c::realOut");
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(c);
    double expected = c->get_value();
    REQUIRE(expected < 4.0);
    for (int i = 0; i < 20; ++i) {
        const double previous = expected;
        sim->step();
        CHECK_THAT(a->get_value(), WithinAbs(0.25 * previous, 1e-12));
        CHECK_THAT(b->get_value(), WithinAbs(1.0 + 0.25 * previous, 1e-12));
        expected = 2.0 + 0.5 * previous;
        CHECK_THAT(c->get_value(), WithinAbs(expected, 1e-12));
    }
    CHECK_THAT(c->get_value(), WithinAbs(4.0, 1e-5));
    sim->terminate();
}

TEST_CASE("Gauss-Seidel matches the analytical damped oscillator with three physical FMUs", "[gauss_seidel]")
{
    constexpr double stepSize = 0.001;
    const auto fmuDir = std::filesystem::path(DATA_FOLDER) / "fmus/1.0/mass_spring_damper";
    simulation_structure structure;
    structure.add_model("mass", fmuDir / "Mass.fmu");
    structure.add_model("damper", fmuDir / "Damper.fmu");
    structure.add_model("spring", fmuDir / "Spring.fmu");
    structure.make_connection<double>("mass::out_l_u", "spring::dis_xx");
    structure.make_connection<double>("mass::out_f_u", "damper::lv_0");
    structure.make_connection<double>("spring::for_xx", "mass::in_l_u");
    structure.make_connection<double>("damper::df_0", "mass::in_f_u");
    structure.add_parameter_set("oscillator", {
        {"mass::initialPositionX", 6.0},
        {"mass::mass", 10.0},
        {"mass::inGravityField", false},
        {"mass::enableDrag", false},
        {"mass::livePlotting", false},
        {"spring::springStiffness", 5.0},
        {"spring::zeroForceLength", 5.0},
        {"spring::livePlotting", false},
        {"damper::dampingCoefficient", 2.0},
        {"damper::livePlotting", false},
    });

    auto sim = structure.load(std::make_unique<gauss_seidel_algorithm>(
        stepSize, std::vector<std::string>{"spring", "damper", "mass"}));
    sim->init("oscillator");
    auto* position = sim->get_real_property("mass::out_l_u");
    auto* velocity = sim->get_real_property("mass::out_f_u");
    REQUIRE(position);
    REQUIRE(velocity);
    REQUIRE_THAT(position->get_value(), WithinAbs(6.0, 1e-12));

    for (int i = 1; i <= 40; ++i) {
        const double t = sim->step(100);
        const double decay = std::exp(-0.1 * t);
        const double sine = std::sin(0.7 * t);
        const double expectedPosition = 5.0 + decay * (std::cos(0.7 * t) + sine / 7.0);
        const double expectedVelocity = -decay * (0.5 / 0.7) * sine;
        CHECK_THAT(t, WithinAbs(i * 100 * stepSize, 1e-12));
        CHECK_THAT(position->get_value(), WithinAbs(expectedPosition, 0.002));
        CHECK_THAT(velocity->get_value(), WithinAbs(expectedVelocity, 0.002));
    }
    sim->terminate();
}

TEST_CASE("Gauss-Seidel validates the schedule and requires a reset after a partial sweep", "[gauss_seidel]")
{
    CHECK_THROWS_AS(gauss_seidel_algorithm(0.0, {"a"}), std::invalid_argument);
    CHECK_THROWS_AS(gauss_seidel_algorithm(std::numeric_limits<double>::infinity(), {"a"}), std::invalid_argument);
    CHECK_THROWS_AS(gauss_seidel_algorithm(0.01, {"a", "a"}), std::invalid_argument);

    nova_engine sim(std::make_unique<gauss_seidel_algorithm>(0.01, std::vector<std::string>{"a", "b"}));
    REQUIRE_THROWS_AS(sim.add_slave(std::make_unique<test_model>("extra")), std::invalid_argument);
    CHECK(sim.get_instance("extra") == nullptr);

    auto a = std::make_unique<test_model>("a");
    auto b = std::make_unique<test_model>("b");
    auto* first = a.get();
    auto* second = b.get();
    sim.add_slave(std::move(b));
    sim.add_slave(std::move(a));
    sim.init();

    second->rejectStep = true;
    REQUIRE_THROWS_AS(sim.step(), std::runtime_error);
    CHECK(sim.time() == 0.0);
    CHECK(sim.iterations() == 0);
    CHECK(first->steps == 1);
    REQUIRE_THROWS_AS(sim.step(), std::runtime_error);
    CHECK(first->steps == 1);

    sim.reset();
    sim.init();
    second->rejectStep = false;
    CHECK(sim.step() == 0.01);
    CHECK(first->steps == 1);
    CHECK(second->steps == 1);
    sim.terminate();
}
