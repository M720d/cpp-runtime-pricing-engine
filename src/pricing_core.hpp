#pragma once

#include <memory>
#include <stdexcept>  // for std::runtime_error thrown in PricingEngine

// ---------------------------------------------------------------------------
// OptionSpec
// ---------------------------------------------------------------------------
// Plain data struct that describes a single option contract.
// All pricing models and the engine receive this as input.
// Adding new fields here (e.g. dividends) would propagate everywhere.
// ---------------------------------------------------------------------------
struct OptionSpec {
    double spot     = 0.0;   // current underlying price  (S)
    double strike   = 0.0;   // option strike price       (K)
    double rate     = 0.0;   // risk-free interest rate   (r, annualised)
    double vol      = 0.0;   // volatility (sigma, annualised)
    double maturity = 0.0;   // time to expiry in years   (T)
    bool   is_call  = true;  // true = call, false = put
    bool   is_american = false; // false = European (default), true = American
                                // American: holder may exercise at any time before T
                                // European: holder may only exercise at T
};

// ---------------------------------------------------------------------------
// GreeksResult
// ---------------------------------------------------------------------------
// Returned by any model's greeks() method.
// Each field is a first-order sensitivity of the option price to one input:
//   delta : dP/dS      — how much price moves per $1 move in the underlying
//   gamma : d²P/dS²    — rate of change of delta; convexity of price vs spot
//   vega  : dP/dsigma  — sensitivity to a 1-unit change in vol (not 1%)
//   theta : -dP/dT     — daily time decay (sign: positive = value erodes)
//   rho   : dP/dr      — sensitivity to a 1-unit change in interest rate
// ---------------------------------------------------------------------------
struct GreeksResult {
    double delta = 0.0;
    double gamma = 0.0;
    double vega  = 0.0;
    double theta = 0.0;
    double rho   = 0.0;
};

// ---------------------------------------------------------------------------
// validate_option_spec
// ---------------------------------------------------------------------------
// Called before any pricing or greeks computation.
// Throws std::invalid_argument with a descriptive message if any field is
// outside the domain where the pricing math is defined.
// Centralising validation here means every model gets it for free via
// PricingEngine, and unit tests can check bad inputs in one place.
// ---------------------------------------------------------------------------
void validate_option_spec(const OptionSpec& opt);

// ---------------------------------------------------------------------------
// PricingModel  (Abstract Base Class)
// ---------------------------------------------------------------------------
// Defines the interface every pricing algorithm must satisfy.
//
// Subclass hierarchy:
//   PricingModel
//     ├── BlackScholesModel   — closed-form analytic solution
//     ├── BinomialModel       — numerical lattice (supports American)
//     └── MonteCarloModel     — stochastic simulation (European only)
//
// greeks() has a DEFAULT implementation in the base class that uses
// finite differences (bumping inputs and re-pricing).  That means any new
// subclass you add automatically gets working Greeks without extra code.
// BlackScholesModel overrides greeks() with the exact analytic formulas
// because they are both faster and more accurate.
// ---------------------------------------------------------------------------
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
