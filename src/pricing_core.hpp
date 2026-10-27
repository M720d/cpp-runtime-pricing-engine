#pragma once

#include <memory>
#include <stdexcept>  // for std::runtime_error thrown in PricingEngine
#include "types.hpp"


using qa::OptionSpec;
using qa::GreeksResult;
// ---------------------------------------------------------------------------
// validate_option_spec
// ---------------------------------------------------------------------------
// Called before any pricing or greeks computation.
// Throws std::idnvalid_argument with a descriptive message if any field is
// outside the domain where the pricing math is defined.
// Centralising validation here means every model gets it for free via
// PricingEngddddfine, and unit tests can check bad inputs in one place.
// ---------------------------------------------------------------------------
void validate_option_spec(const OptionSpec& opt);

class PricingModel {
public:
    // Must be implemented by every concrete model.
    virtual double price(const OptionSpec& opt) const = 0;

    // Default: central finite-difference bumping on price().
    // Override in subclasses when you have a faster/exact implementation.
    virtual GreeksResult greeks(const OptionSpec& opt) const;

    virtual ~PricingModel() = default;
};

// ---------------------------------------------------------------------------
// BlackScholesModel
// ---------------------------------------------------------------------------
// Implements the Black-Scholes-Merton closed-form formula for European
// calls and puts.  Only valid for European options (is_american ignored).
// Also overrides greeks() with the exact analytic Greek formulas.
// ---------------------------------------------------------------------------
class BlackScholesModel : public PricingModel {
public:
    double price(const OptionSpec& opt) const override;
    GreeksResult greeks(const OptionSpec& opt) const override; // analytic override
};

// ---------------------------------------------------------------------------
// BinomialModel
// ---------------------------------------------------------------------------
// Cox-Ross-Rubinstein recombining binomial tree.
// Supports both European and American options via opt.is_american.
// More steps = more accurate but slower (O(N²) time/memory).
// Greeks fall back to the base-class finite-difference implementation.
// ---------------------------------------------------------------------------
class BinomialModel : public PricingModel {
    int steps_; // number of time steps in the tree (e.g. 200)
public:
    explicit BinomialModel(int steps);
    double price(const OptionSpec& opt) const override;
    // greeks() inherited from PricingModel (finite differences)
};

// ---------------------------------------------------------------------------
// MonteCarloModel
// ---------------------------------------------------------------------------
// Simulates many Geometric Brownian Motion paths for the underlying and
// averages the discounted payoff — the classic risk-neutral MC approach.
// paths_  : number of independent simulated price paths
// steps_  : GBM time discretisation steps per path (more = smoother)
// seed_   : RNG seed — FIXED so results are reproducible across calls
//           (important for unit tests: same seed always gives same price)
// Only European options are supported. American would require Longstaff-
// Schwartz regression which is a larger separate upgrade.
// ---------------------------------------------------------------------------
class MonteCarloModel : public PricingModel {
    int      paths_;
    int      steps_;
    unsigned seed_;
public:
    explicit MonteCarloModel(int paths = 100000, int steps = 252, unsigned seed = 42);
    double price(const OptionSpec& opt) const override;
    // greeks() inherited from PricingModel (finite differences)
};

// ---------------------------------------------------------------------------
// PricingEngine  (Facade)
// ---------------------------------------------------------------------------
// The entry point for external callers.  Holds a PricingModel via shared_ptr
// so models can be swapped at runtime without rebuilding the engine.
//
// Flow for a price() call:
//   1. Guard: model must be set
//   2. validate_option_spec — throws on bad inputs before touching the model
//   3. Delegate to model_->price(opt)
//
// greeks() follows the same flow but calls model_->greeks(opt).
// ---------------------------------------------------------------------------
class PricingEngine {
    std::shared_ptr<PricingModel> model_;
public:
    PricingEngine() = default;

    // Swap the active model at runtime (shared ownership via shared_ptr).
    void set_shared_model(std::shared_ptr<PricingModel> m) {
        model_ = std::move(m);
    }

    // Price the option: validate inputs, then delegate to the active model.
    double price(const OptionSpec& opt) const {
        if (!model_) {
            throw std::runtime_error("No model set — call set_shared_model() first");
        }
        validate_option_spec(opt);   // centralised guard; throws on bad data
        return model_->price(opt);
    }

    // Compute all 5 Greeks: validate inputs, then delegate to the active model.
    GreeksResult greeks(const OptionSpec& opt) const {
        if (!model_) {
            throw std::runtime_error("No model set — call set_shared_model() first");
        }
        validate_option_spec(opt);
        return model_->greeks(opt);
    }
};
