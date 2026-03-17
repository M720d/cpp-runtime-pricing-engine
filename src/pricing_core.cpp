#include "pricing_core.hpp"

#include <cmath>        // std::exp, std::log, std::sqrt, std::pow, std::erfc, M_PI
#include <algorithm>    // std::max
#include <vector>       // std::vector (used by BinomialModel)
#include <random>       // std::mt19937_64, std::normal_distribution (MonteCarloModel)
#include <stdexcept>    // std::invalid_argument, std::logic_error
#include <string>       // std::to_string (used in error messages)

// ---------------------------------------------------------------------------
// norm_cdf  (file-local helper)
// ---------------------------------------------------------------------------
// Cumulative distribution function of the standard normal N(0,1).
// Used by both BlackScholesModel::price and BlackScholesModel::greeks.
//
// Formula: Phi(x) = 0.5 * erfc(-x / sqrt(2))
//   erfc = complementary error function, available in <cmath>
//   This is numerically equivalent to the integral of the Gaussian PDF
//   from -inf to x.
// ---------------------------------------------------------------------------
static double norm_cdf(double x) {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

// ---------------------------------------------------------------------------
// norm_pdf  (file-local helper)
// ---------------------------------------------------------------------------
// Probability density function of the standard normal N(0,1).
// Formula: phi(x) = exp(-x^2/2) / sqrt(2*pi)
// Used by BlackScholesModel::greeks for gamma, vega, theta.
// ---------------------------------------------------------------------------
static double norm_pdf(double x) {
    return std::exp(-0.5 * x * x) / std::sqrt(2.0 * M_PI);
}

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
    if (opt.spot <= 0.0)
        throw std::invalid_argument(
            "spot must be > 0, got: " + std::to_string(opt.spot));
    if (opt.strike <= 0.0)
        throw std::invalid_argument(
            "strike must be > 0, got: " + std::to_string(opt.strike));
    if (opt.vol <= 0.0)
        throw std::invalid_argument(
            "vol must be > 0, got: " + std::to_string(opt.vol));
    if (opt.maturity <= 0.0)
        throw std::invalid_argument(
            "maturity must be > 0, got: " + std::to_string(opt.maturity));
    if (opt.rate < -1.0)
        throw std::invalid_argument(
            "rate is implausibly negative, got: " + std::to_string(opt.rate));
}

// ===========================================================================
// PricingModel::greeks  (base-class default — finite differences)
// ===========================================================================
// This is the FALLBACK Greeks implementation that every subclass inherits
// automatically.  It estimates each Greek by bumping one input, re-pricing
// twice (up and down), and dividing by the bump size — the standard
// central-difference approximation.
//
// Why central differences?  They are O(h²) accurate, meaning halving the
// bump size reduces error by 4x, vs only 2x for forward differences.
//
// Bump sizes are chosen as a fraction (1e-4) of the current parameter value:
//   - Relative bumps (1e-4 * S, 1e-4 * vol, etc.) scale automatically with
//     the option's parameters so we don't need to tune per-contract.
//   - Rate bump is absolute (1e-4) since rate near 0 would give a zero bump.
//
// IMPORTANT: this method calls price() on *this*, so it uses whatever
// concrete model (BS, Binomial, MC) is the actual subclass — polymorphism
// in action.
// ===========================================================================
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

// ===========================================================================
// BlackScholesModel::price
// ===========================================================================
// The Black-Scholes-Merton closed-form formula for European options.
//
// Key intermediate quantities:
//   d1 = [ln(S/K) + (r + sigma²/2)*T] / (sigma*sqrt(T))
//   d2 = d1 - sigma*sqrt(T)
//
// Call price:  S * N(d1) - K*exp(-rT) * N(d2)
// Put price:   K*exp(-rT) * N(-d2) - S * N(-d1)
//   where N() = norm_cdf = cumulative standard normal CDF.
// ===========================================================================
double BlackScholesModel::price(const OptionSpec& opt) const {
    double S     = opt.spot;
    double K     = opt.strike;
    double r     = opt.rate;
    double sigma = opt.vol;
    double T     = opt.maturity;

    // Edge case: handle degenerate inputs gracefully (T=0 or sigma=0).
    // In these cases the option is worth its discounted intrinsic value.
    if (T <= 0.0 || sigma <= 0.0) {
        double intrinsic = opt.is_call
            ? std::max(S - K, 0.0)
            : std::max(K - S, 0.0);
        return intrinsic * std::exp(-r * T);
    }

    double sqrtT = std::sqrt(T);

    // d1 and d2 are the standardised log-moneyness adjusted for drift and vol.
    double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * sqrtT);
    double d2 = d1 - sigma * sqrtT;  // d2 = d1 minus one vol-adjusted std dev

    if (opt.is_call) {
        // Call: probability-weighted present value of receiving S minus paying K
        return S * norm_cdf(d1) - K * std::exp(-r * T) * norm_cdf(d2);
    } else {
        // Put: mirror image using put-call symmetry N(-d) = 1 - N(d)
        return K * std::exp(-r * T) * norm_cdf(-d2) - S * norm_cdf(-d1);
    }
}

// ===========================================================================
// BlackScholesModel::greeks  (analytic override)
// ===========================================================================
// Exact closed-form partial derivatives of the BS price formula.
// Much faster and more precise than the finite-difference fallback.
//
// All formulas share the same d1/d2 intermediates from price().
//
// Symmetric Greeks (same for calls and puts):
//   gamma = phi(d1) / (S * sigma * sqrt(T))
//   vega  = S * sqrt(T) * phi(d1)
//   where phi() = norm_pdf = standard normal probability density.
//
// Asymmetric Greeks (sign differs between calls and puts):
//   delta_call =  N(d1),    delta_put = N(d1) - 1  [always in (0,1) / (-1,0)]
//   theta_call = -(S*phi(d1)*sigma)/(2*sqrt(T)) - r*K*exp(-rT)*N(d2)
//   theta_put  = -(S*phi(d1)*sigma)/(2*sqrt(T)) + r*K*exp(-rT)*N(-d2)
//   rho_call   =  K*T*exp(-rT)*N(d2)
//   rho_put    = -K*T*exp(-rT)*N(-d2)
// ===========================================================================
GreeksResult BlackScholesModel::greeks(const OptionSpec& opt) const {
    double S     = opt.spot;
    double K     = opt.strike;
    double r     = opt.rate;
    double sigma = opt.vol;
    double T     = opt.maturity;
    double sqrtT = std::sqrt(T);

    // Recompute d1/d2 (same as in price()).
    double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * sqrtT);
    double d2 = d1 - sigma * sqrtT;

    // Discount factor for the strike leg.
    double df = std::exp(-r * T);  // = e^{-rT}

    // Normal PDF at d1 — appears in gamma, vega, and theta.
    double phi_d1 = norm_pdf(d1);

    GreeksResult g;

    // ------------------------------------------------------------------
    // Gamma and Vega are the same for calls and puts (put-call parity).
    // ------------------------------------------------------------------
    g.gamma = phi_d1 / (S * sigma * sqrtT);
    g.vega  = S * sqrtT * phi_d1;

    // ------------------------------------------------------------------
    // Delta, Theta, Rho differ by sign between call and put.
    // ------------------------------------------------------------------
    if (opt.is_call) {
        g.delta = norm_cdf(d1);                                            // in (0, 1)
        g.theta = -(S * phi_d1 * sigma) / (2.0 * sqrtT)                   // vol decay
                  - r * K * df * norm_cdf(d2);                             // interest effect
        g.rho   = K * T * df * norm_cdf(d2);                              // positive for calls
    } else {
        g.delta = norm_cdf(d1) - 1.0;                                     // in (-1, 0)
        g.theta = -(S * phi_d1 * sigma) / (2.0 * sqrtT)                   // vol decay (same)
                  + r * K * df * norm_cdf(-d2);                            // interest effect flips
        g.rho   = -K * T * df * norm_cdf(-d2);                            // negative for puts
    }

    return g;
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
    int    N     = std::max(1, steps_);
    double S0    = opt.spot;
    double K     = opt.strike;
    double r     = opt.rate;
    double sigma = opt.vol;
    double T     = opt.maturity;

    double dt   = T / N;
    double u    = std::exp(sigma * std::sqrt(dt));  // up factor
    double d    = 1.0 / u;                          // down factor (recombining)
    double disc = std::exp(-r * dt);                // one-step discount factor
    double p    = (std::exp(r * dt) - d) / (u - d); // risk-neutral up probability

    // --- Step 1: Fill terminal payoffs ---
    // option_values[i] = payoff if there were i up-moves and (N-i) down-moves
    std::vector<double> option_values(N + 1);
    for (int i = 0; i <= N; ++i) {
        double ST = S0 * std::pow(u, i) * std::pow(d, N - i); // terminal spot
        option_values[i] = opt.is_call
            ? std::max(ST - K, 0.0)   // call payoff: max(S-K, 0)
            : std::max(K - ST, 0.0);  // put  payoff: max(K-S, 0)
    }

    // --- Step 2: Roll backwards through the tree ---
    for (int step = N - 1; step >= 0; --step) {
        for (int i = 0; i <= step; ++i) {
            // Risk-neutral expected value discounted one step
            double continuation = disc * (p * option_values[i + 1]
                                        + (1.0 - p) * option_values[i]);

            if (opt.is_american) {
                // For American options: the holder can exercise NOW (intrinsic)
                // or wait (continuation).  We take the maximum — this is the
                // early-exercise feature that makes Americans worth >= Europeans.
                //
                // Spot at this node: S0 * u^(up_moves) * d^(down_moves)
                // In backward induction at step `step`, node `i` has i up-moves.
                double S_node  = S0 * std::pow(u, i) * std::pow(d, step - i);
                double exercise = opt.is_call
                    ? std::max(S_node - K, 0.0)
                    : std::max(K - S_node, 0.0);
                option_values[i] = std::max(continuation, exercise);
            } else {
                // European: no early exercise — just take the discounted expectation.
                option_values[i] = continuation;
            }
        }
    }

    // After rolling all the way back, index 0 is the root of the tree.
    return option_values[0];
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
