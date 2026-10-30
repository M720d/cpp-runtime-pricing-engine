#include "pricing_core.hpp"

#include <cmath>        // std::exp, std::log, std::sqrt, std::pow, std::erfc, M_PI
#include <algorithm>    // std::max
#include <vector>       // std::vector (used by BinomialModel)
#include <random>       // std::mt19937_64, std::normal_distribution (MonteCarloModel)
#include <stdexcept>    // std::invalid_argument, std::logic_error
#include <string>       // std::to_string (used in error messages)
#include "math_utils.hpp"
#include "black_scholes.hpp"
#include "types.hpp" 
#include "binomial.hpp" 


// ===========================================================================
// validate_option_spec
// ===========================================================================
// Checks that all OptionSpec fields are within the domain where pricing
// math is defined.  Throws std::invalid_argument with a clear message if not.
//
// Rules:
//   spot     > 0  (price of a real asset can't be zero or negative)
//   strike   > 0  (same reasoning)
//   vol      > 0  (zero or negative vol makes BS undefined; no meaningful model)
//   maturity > 0  (expired options are worth their intrinsic — not our job here)
//   rate     >= -1 (allow mildly negative rates; reject implausibly extreme values)
// ===========================================================================
void validate_option_spec(const OptionSpec& opt) {
    const qa::Status checkStatus = check(opt);

    if(checkStatus!=qa::Status::ok){
        throw std::invalid_argument(qa::to_string(checkStatus));
    }
}


GreeksResult PricingModel::greeks(const OptionSpec& opt) const {
    GreeksResult g;

    // Bump sizes — small enough to be accurate, large enough to avoid
    // catastrophic cancellation in floating-point subtraction.
    const double dS   = opt.spot    * 1e-4;  // spot bump
    const double dSig = opt.vol     * 1e-4;  // vol bump
    const double dT   = opt.maturity * 1e-4; // maturity bump
    const double dR   = 1e-4;                // rate bump (absolute)

    // ------------------------------------------------------------------
    // Delta = dP/dS
    // Central difference: (P(S+dS) - P(S-dS)) / (2*dS)
    // Interpretation: for a $1 rise in spot, price moves by delta dollars.
    // ------------------------------------------------------------------
    {
        OptionSpec up = opt; up.spot = opt.spot + dS;
        OptionSpec dn = opt; dn.spot = opt.spot - dS;
        g.delta = (price(up) - price(dn)) / (2.0 * dS);
    }

    // ------------------------------------------------------------------
    // Gamma = d²P/dS²
    // Second-order central difference: (P(S+dS) - 2*P(S) + P(S-dS)) / dS²
    // Interpretation: rate of change of delta — how quickly delta moves
    // as the spot moves.  Always positive for vanilla long options.
    // ------------------------------------------------------------------
    {
        OptionSpec up = opt; up.spot = opt.spot + dS;
        OptionSpec dn = opt; dn.spot = opt.spot - dS;
        g.gamma = (price(up) - 2.0 * price(opt) + price(dn)) / (dS * dS);
    }

    // ------------------------------------------------------------------
    // Vega = dP/d(sigma)
    // Units: change in price per 1 unit change in vol (i.e., per 100% vol).
    // To get "per 1% vol" divide by 100 at the call site.
    // ------------------------------------------------------------------
    {
        OptionSpec up = opt; up.vol = opt.vol + dSig;
        OptionSpec dn = opt; dn.vol = opt.vol - dSig;
        g.vega = (price(up) - price(dn)) / (2.0 * dSig);
    }

    // ------------------------------------------------------------------
    // Theta = -dP/dT
    // The NEGATIVE of the maturity sensitivity, because time moves forward
    // while T (time to expiry) decreases.  Theta > 0 means the option
    // loses value as time passes (which is true for long vanilla options).
    //
    // Guard: bump-down maturity can't go below a minimum positive value,
    // so we clamp it to avoid triggering validate_option_spec.
    // ------------------------------------------------------------------
    {
        OptionSpec up = opt; up.maturity = opt.maturity + dT;
        OptionSpec dn = opt; dn.maturity = std::max(dT * 0.5, opt.maturity - dT);
        g.theta = -(price(up) - price(dn)) / (2.0 * dT);
    }

    // ------------------------------------------------------------------
    // Rho = dP/dr
    // Sensitivity to the risk-free rate.
    // Calls have positive rho (higher rate => higher call value).
    // Puts have negative rho.
    // ------------------------------------------------------------------
    {
        OptionSpec up = opt; up.rate = opt.rate + dR;
        OptionSpec dn = opt; dn.rate = opt.rate - dR;
        g.rho = (price(up) - price(dn)) / (2.0 * dR);
    }

    return g;
}


double BlackScholesModel::price( const OptionSpec &o)const {
    validate_option_spec(o);
    return qa::bs::price(o);
}

GreeksResult BlackScholesModel::greeks(const OptionSpec&o)const{
    validate_option_spec(o);
    return qa::bs::greeks(o);
}




// ===========================================================================
// BinomialModel::BinomialModel
// ===========================================================================
BinomialModel::BinomialModel(int steps)
    : steps_(steps) {}

// ===========================================================================
// BinomialModel::price
// ===========================================================================
// Cox-Ross-Rubinstein (CRR) recombining binomial lattice.
//
// Idea: divide T into N equal time steps of size dt = T/N.
// At each step the underlying can move:
//   up by factor u = exp(sigma * sqrt(dt))
//   down by factor d = 1/u   (recombining: up then down = down then up)
//
// The risk-neutral probability of an up-move:
//   p = (exp(r*dt) - d) / (u - d)
//
// Algorithm:
//   1. Build terminal payoffs at time T (the option's intrinsic values at
//      every possible terminal spot price S0 * u^i * d^(N-i)).
//   2. Roll backwards one step at a time: at each node the option value is
//      disc * (p * value_up + (1-p) * value_down).
//   3. For American options: at each node also compute the immediate exercise
//      value (intrinsic) and take the MAX of hold vs exercise.
//      For European: skip that max — just discount the expected continuation.
//
// The final option_values[0] after rolling all the way back is the price.
// ===========================================================================
double BinomialModel::price(const OptionSpec& opt) const {
    return qa::binomial::price(opt,steps_);
}

// ===========================================================================
// MonteCarloModel constructor
// ===========================================================================
MonteCarloModel::MonteCarloModel(int paths, int steps, unsigned seed)
    : paths_(paths), steps_(steps), seed_(seed) {}

// ===========================================================================
// MonteCarloModel::price
// ===========================================================================
// Geometric Brownian Motion (GBM) Monte Carlo pricing.
//
// Under the risk-neutral measure the underlying follows:
//   dS = r*S*dt + sigma*S*dW    (dW = Brownian increment)
//
// Discretised over a small step dt:
//   S_{t+dt} = S_t * exp( (r - sigma²/2)*dt  +  sigma*sqrt(dt)*Z )
//   where Z ~ N(0,1) is a standard normal random number.
//
// The (r - sigma²/2) term is the Ito correction — without it the expected
// log-return would be too high due to Jensen's inequality.
//
// Algorithm:
//   For each of `paths_` independent paths:
//     1. Start at S0.
//     2. Simulate `steps_` GBM increments using draws from N(0,1).
//     3. Compute the terminal payoff max(S_T - K, 0) or max(K - S_T, 0).
//   Average all payoffs and discount by e^{-rT}.
//
// The seed is fixed so the same MonteCarloModel instance gives the same
// price every time — essential for reproducibility and unit testing.
//
// Limitation: American options require storing all path values and running
// a regression-based backward pass (Longstaff-Schwartz).  That is a
// significantly larger addition; for now we throw a clear error.
// ===========================================================================
double MonteCarloModel::price(const OptionSpec& opt) const {
    if (opt.is_american) {
        throw std::logic_error(
            "MonteCarloModel does not yet support American options. "
            "Use BinomialModel for American pricing.");
    }

    double S0    = opt.spot;
    double K     = opt.strike;
    double r     = opt.rate;
    double sigma = opt.vol;
    double T     = opt.maturity;

    double dt     = T / steps_;
    // Pre-compute the per-step drift and vol components of the GBM exponent.
    double drift  = (r - 0.5 * sigma * sigma) * dt;  // Ito-corrected drift
    double vol_dt = sigma * std::sqrt(dt);             // vol scaled by sqrt(dt)
    double disc   = std::exp(-r * T);                  // full discount to time 0

    // Seeded RNG: mt19937_64 is a high-quality 64-bit Mersenne Twister.
    // The seed is stored as a member so it's the same on every call.
    std::mt19937_64 rng(seed_);
    std::normal_distribution<double> Z(0.0, 1.0);

    double sum_payoff = 0.0;

    for (int path = 0; path < paths_; ++path) {
        double S = S0;

        // Simulate one GBM path: step the spot forward `steps_` times.
        for (int t = 0; t < steps_; ++t) {
            S *= std::exp(drift + vol_dt * Z(rng));
        }

        // Terminal payoff at path's final spot price.
        double payoff = opt.is_call
            ? std::max(S - K, 0.0)
            : std::max(K - S, 0.0);

        sum_payoff += payoff;
    }

    // Risk-neutral price = E[payoff under Q] discounted to today.
    return disc * sum_payoff / static_cast<double>(paths_);
}
