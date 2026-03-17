// ---------------------------------------------------------------------------
// bindings.cpp — pybind11 glue between C++ pricing engine and Python
// ---------------------------------------------------------------------------
// This file is the ONLY place where C++ types are made visible to Python.
// Every struct, class, and method that Python needs must be registered here.
//
// pybind11 works by building a Python extension module (a .so/.pyd file).
// When Python does `import pricing_core`, it loads this module and the
// PYBIND11_MODULE block runs, registering all the types.
//
// Pattern for each type:
//   py::class_<CppType>(m, "PythonName")
//     .def(py::init<...>())        // constructor
//     .def_readwrite("field", ...) // expose a data member
//     .def("method", &CppType::method)  // expose a method
//
// For class hierarchies (inheritance), we pass the base class as an extra
// template argument:
//   py::class_<Derived, Base, std::shared_ptr<Derived>>(m, "Derived")
// The shared_ptr is needed so Python's ownership model (reference counting)
// can work with C++ shared ownership via set_shared_model().
// ---------------------------------------------------------------------------

#include <pybind11/pybind11.h>
#include <memory>
#include <string>          // for std::to_string in __repr__
#include "pricing_core.hpp"

namespace py = pybind11;

PYBIND11_MODULE(pricing_core, m) {
    m.doc() = "C++ option pricing engine with runtime-switchable models. "
              "Models: BlackScholesModel, BinomialModel, MonteCarloModel. "
              "All models support price() and greeks().";

    // -----------------------------------------------------------------------
    // OptionSpec
    // -----------------------------------------------------------------------
    // Exposed as a plain Python object with attribute access.
    // Usage:
    //   opt = pricing_core.OptionSpec()
    //   opt.spot = 100.0
    //   opt.is_american = True  # NEW — defaults to False
    // -----------------------------------------------------------------------
    py::class_<OptionSpec>(m, "OptionSpec")
        .def(py::init<>())
        .def_readwrite("spot",        &OptionSpec::spot)
        .def_readwrite("strike",      &OptionSpec::strike)
        .def_readwrite("rate",        &OptionSpec::rate)
        .def_readwrite("vol",         &OptionSpec::vol)
        .def_readwrite("maturity",    &OptionSpec::maturity)
        .def_readwrite("is_call",     &OptionSpec::is_call)
        .def_readwrite("is_american", &OptionSpec::is_american);  // NEW

    // -----------------------------------------------------------------------
    // GreeksResult
    // -----------------------------------------------------------------------
    // Returned by model.greeks(opt) or engine.greeks(opt).
    // All 5 standard option Greeks are accessible as attributes.
    // __repr__ lets you print the object for quick inspection.
    //
    // Usage:
    //   g = bs.greeks(opt)
    //   print(g.delta, g.gamma, g.vega, g.theta, g.rho)
    //   print(g)   # calls __repr__
    // -----------------------------------------------------------------------
    py::class_<GreeksResult>(m, "GreeksResult")
        .def(py::init<>())
        .def_readwrite("delta", &GreeksResult::delta)
        .def_readwrite("gamma", &GreeksResult::gamma)
        .def_readwrite("vega",  &GreeksResult::vega)
        .def_readwrite("theta", &GreeksResult::theta)
        .def_readwrite("rho",   &GreeksResult::rho)
        .def("__repr__", [](const GreeksResult& g) {
            // Produce a readable Python-style string representation.
            return "GreeksResult(delta="  + std::to_string(g.delta)
                 + ", gamma=" + std::to_string(g.gamma)
                 + ", vega="  + std::to_string(g.vega)
                 + ", theta=" + std::to_string(g.theta)
                 + ", rho="   + std::to_string(g.rho) + ")";
        });

    // -----------------------------------------------------------------------
    // PricingModel  (abstract base)
    // -----------------------------------------------------------------------
    // Exposed only so Python can hold a reference to a base-class pointer
    // and pass it to set_shared_model().  You never instantiate this directly.
    // -----------------------------------------------------------------------
    py::class_<PricingModel, std::shared_ptr<PricingModel>>(m, "PricingModel");

    // -----------------------------------------------------------------------
    // BlackScholesModel
    // -----------------------------------------------------------------------
    // Analytic European option pricing.
    // price()  — Black-Scholes closed-form formula
    // greeks() — exact analytic Greeks (overrides base FD implementation)
    //
    // Usage:
    //   bs = pricing_core.BlackScholesModel()
    //   p  = bs.price(opt)
    //   g  = bs.greeks(opt)
    // -----------------------------------------------------------------------
    py::class_<BlackScholesModel, PricingModel, std::shared_ptr<BlackScholesModel>>(
        m, "BlackScholesModel")
        .def(py::init<>())
        .def("price",  &BlackScholesModel::price,
             "Compute European option price via Black-Scholes formula.")
        .def("greeks", &BlackScholesModel::greeks,
             "Compute all 5 Greeks analytically (exact closed-form).");

    // -----------------------------------------------------------------------
    // BinomialModel
    // -----------------------------------------------------------------------
    // CRR binomial lattice — supports European AND American options.
    // Greeks fall back to finite differences (from PricingModel base).
    //
    // Constructor argument:
    //   steps — number of time steps in the tree (more = more accurate)
    //           typical usage: BinomialModel(200) for a good speed/accuracy tradeoff
    //
    // Usage:
    //   binom = pricing_core.BinomialModel(200)
    //   opt.is_american = True
    //   p = binom.price(opt)   # American put
    //   g = binom.greeks(opt)  # finite-difference Greeks
    // -----------------------------------------------------------------------
    py::class_<BinomialModel, PricingModel, std::shared_ptr<BinomialModel>>(
        m, "BinomialModel")
        .def(py::init<int>(), py::arg("steps"),
             "Create a binomial tree with the given number of steps.")
        .def("price",  &BinomialModel::price,
             "Compute option price (European or American) via binomial lattice.")
        .def("greeks", &BinomialModel::greeks,
             "Compute all 5 Greeks via central finite differences.");

    // -----------------------------------------------------------------------
    // MonteCarloModel
    // -----------------------------------------------------------------------
    // GBM simulation — European options only.
    // Greeks fall back to finite differences (from PricingModel base).
    //
    // Constructor arguments (all have defaults so Python can call MonteCarloModel()):
    //   paths — number of simulated GBM paths (more = lower variance, slower)
    //   steps — GBM time discretisation steps per path
    //   seed  — RNG seed for reproducibility (same seed → same price every time)
    //
    // Usage:
    //   mc = pricing_core.MonteCarloModel()               # defaults
    //   mc = pricing_core.MonteCarloModel(paths=50000)    # keyword args
    //   p  = mc.price(opt)
    // -----------------------------------------------------------------------
    py::class_<MonteCarloModel, PricingModel, std::shared_ptr<MonteCarloModel>>(
        m, "MonteCarloModel")
        .def(py::init<int, int, unsigned>(),
             py::arg("paths") = 100000,
             py::arg("steps") = 252,
             py::arg("seed")  = 42,
             "Create a Monte Carlo pricer with the given paths, steps, and RNG seed.")
        .def("price",  &MonteCarloModel::price,
             "Compute European option price via GBM Monte Carlo simulation.")
        .def("greeks", &MonteCarloModel::greeks,
             "Compute all 5 Greeks via central finite differences.");

    // -----------------------------------------------------------------------
    // PricingEngine  (facade)
    // -----------------------------------------------------------------------
    // The recommended entry point for external callers.
    // Owns a model via shared_ptr and adds centralised input validation.
    //
    // Usage:
    //   engine = pricing_core.PricingEngine()
    //   engine.set_shared_model(pricing_core.BlackScholesModel())
    //   p = engine.price(opt)
    //   g = engine.greeks(opt)
    //   # swap model at runtime:
    //   engine.set_shared_model(pricing_core.BinomialModel(300))
    //   p2 = engine.price(opt)
    // -----------------------------------------------------------------------
    py::class_<PricingEngine>(m, "PricingEngine")
        .def(py::init<>())
        .def("set_shared_model", &PricingEngine::set_shared_model,
             "Set the active pricing model (can be swapped at runtime).")
        .def("price",  &PricingEngine::price,
             "Validate inputs and compute option price via the active model.")
        .def("greeks", &PricingEngine::greeks,
             "Validate inputs and compute all 5 Greeks via the active model.");
}
