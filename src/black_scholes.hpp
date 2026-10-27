#pragma once
#include <cmath>
#include "math_utils.hpp"
#include "types.hpp"

// Black-Scholes-Merton for European options, as small inline functions.
// PRECONDITION: the caller has already run qa::check(). These functions do not
// re-validate, so they stay cheap enough for tight loops (batch pricing, solvers).
namespace qa::bs
{

    struct Terms
    {
        double d1;
        double d2;
        double sqrt_t;
    };

    [[nodiscard]] inline Terms terms(const OptionSpec &o) noexcept
    {
        const double sqrt_t = std::sqrt(o.maturity);
        const double vol_sqrt_t = o.vol * sqrt_t;
        const double d1 = (std::log(o.spot / o.strike) + (o.rate + 0.5 * o.vol *o.vol  * o.maturity)) / vol_sqrt_t;
        return {d1, d1 - (vol_sqrt_t), sqrt_t};
    }

    [[nodiscard]] inline double price(const OptionSpec &o) noexcept
    {
         Terms t = terms(o);
        // discounted strike
        const double disc_strike = o.strike * std::exp(-o.rate * o.maturity);
        return o.is_call
                   ? o.spot * math::norm_cdf(t.d1) - disc_strike * math::norm_cdf(t.d2)    // call
                   : disc_strike * math::norm_cdf(-t.d2) - o.spot * math::norm_cdf(-t.d1); // put
    }

    [[nodiscard]] inline GreeksResult greeks(const OptionSpec &o) noexcept
    {
        const Terms t = terms(o);
        const double df = std::exp(-o.rate * o.maturity);
        const double pdf_d1 = math::norm_pdf(t.d1);
        const double decay = -(o.spot * pdf_d1 * o.vol) / (2.0 * t.sqrt_t); // same for call and put

        GreeksResult g;
        g.gamma = pdf_d1 / (o.spot * o.vol * t.sqrt_t);
        g.vega = o.spot * t.sqrt_t * pdf_d1;
        if (o.is_call)
        {
            const double n2 = math::norm_cdf(t.d2);
            g.delta = math::norm_cdf(t.d1);
            g.theta = decay - o.rate * o.strike * df * n2;
            g.rho = o.strike * o.maturity * df * n2;
        }
        else
        {
            const double n2 = math::norm_cdf(-t.d2);
            g.delta = math::norm_cdf(t.d1) - 1.0;
            g.theta = decay + o.rate * o.strike * df * n2;
            g.rho = -o.strike * o.maturity * df * n2;
        }
        return g;
    }

} // namespace qa::bs