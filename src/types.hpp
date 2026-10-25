#pragma once
#include <cmath>
#include <cstdint>

namespace qa
{

    // Plain data struct that describes a single option contract.
    struct OptionSpec
    {
        double spot = 0.0;        // current underlying price (S)
        double strike = 0.0;      // option strike price      (K)
        double rate = 0.0;        // risk-free rate, annualised (r)
        double vol = 0.0;         // volatility, annualised (sigma)
        double maturity = 0.0;    // time to expiry in years (T)
        bool is_call = true;      // true = call, false = put
        bool is_american = false; // false = European, true = American
    };

    // First-order sensitivities of the option price.
    struct GreeksResult
    {
        double delta = 0.0; // dP/dS
        double gamma = 0.0; // d2P/dS2
        double vega = 0.0;  // dP/dsigma (per 1.0 of vol, not per 1%)
        double theta = 0.0; // -dP/dT
        double rho = 0.0;   // dP/dr
    };
    // status codes
    enum class Status : std::uint8_t
    {
        ok,
        bad_spot,
        bad_strike,
        bad_vol,
        bad_maturity,
        bad_rate,
    };
    // noexcept meaning the code with never throw
    namespace detail
    {
        // "x > 0" is false for NaN, so "!(x > 0)" rejects NaN. "x <= 0" would let NaN through.
        inline bool positive_finite(double x) noexcept
        {
            return std::isfinite(x) && x > 0.0;
        }
    }

    // nodiscard meaning : Make sure whoever calls this function actually does something with the result."
    [[nodiscard]] inline Status check(const OptionSpec &o) noexcept
    {
        if (!detail::positive_finite(o.strike))
        {
            return Status::bad_strike;
        }
        if (!detail::positive_finite(o.maturity))
        {
            return Status::bad_maturity;
        }
        if (!(std::isfinite(o.rate) && o.rate >= -1.0))
        {
            return Status::bad_rate;
        }
        if (!detail::positive_finite(o.spot))
        {
            return Status::bad_spot;
        }
        if (!detail::positive_finite(o.vol))
        {
            return Status::bad_vol;
        }

        return Status::ok;
    }

    [[nodiscard]] inline const char *to_string(Status s) noexcept
    {
        switch (s)
        {
        case Status::ok:
            return "ok";
        case Status::bad_spot:
            return "spot must be a finite number > 0";
        case Status::bad_strike:
            return "strike must be a finite number > 0";
        case Status::bad_vol:
            return "vol must be a finite number > 0";
        case Status::bad_maturity:
            return "maturity must be a finite number > 0";
        case Status::bad_rate:
            return "rate must be finite and >= -1";
        }

        return "Unknow Status";
    }
}
