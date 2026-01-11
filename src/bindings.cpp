#include <pybind11/pybind11.h>
#include <memory>
#include "pricing_core.hpp"

namespace py = pybind11;

PYBIND11_MODULE(pricing_core, m) {
    m.doc() = "C++ option pricing engine with runtime-switchable models";

    py::class_<OptionSpec>(m, "OptionSpec")
        .def(py::init<>())
        .def_readwrite("spot",     &OptionSpec::spot)
        .def_readwrite("strike",   &OptionSpec::strike)
        .def_readwrite("rate",     &OptionSpec::rate)
        .def_readwrite("vol",      &OptionSpec::vol)
        .def_readwrite("maturity", &OptionSpec::maturity)
        .def_readwrite("is_call",  &OptionSpec::is_call);

    py::class_<PricingModel, std::shared_ptr<PricingModel>>(m, "PricingModel");

    py::class_<BlackScholesModel, PricingModel, std::shared_ptr<BlackScholesModel>>(m, "BlackScholesModel")
        .def(py::init<>())
        .def("price", &BlackScholesModel::price);

    py::class_<BinomialModel, PricingModel, std::shared_ptr<BinomialModel>>(m, "BinomialModel")
        .def(py::init<int>())
        .def("price", &BinomialModel::price);

    py::class_<PricingEngine>(m, "PricingEngine")
        .def(py::init<>())
        .def("set_shared_model", &PricingEngine::set_shared_model)
        .def("price", &PricingEngine::price);
}
