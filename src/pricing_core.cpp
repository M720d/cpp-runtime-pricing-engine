#include "pricing_core.hpp"

#include <cmath>
#include <algorithm>
#include <vector>

static double norm_cdf(double x) {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

double BlackScholesModel::price(const OptionSpec& opt) const {
    double S = opt.spot;
    double K = opt.strike;
    double r = opt.rate;
    double sigma = opt.vol;
    double T = opt.maturity;

    if (T <= 0.0 || sigma <= 0.0) {
        double intrinsic = opt.is_call
            ? std::max(S - K, 0.0)
            : std::max(K - S, 0.0);
        return intrinsic * std::exp(-r * T);
    }

    double sqrtT = std::sqrt(T);
    double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * sqrtT);
    double d2 = d1 - sigma * sqrtT;

    if (opt.is_call) {
        double call = S * norm_cdf(d1) - K * std::exp(-r * T) * norm_cdf(d2);
        return call;
    } else {
        double put = K * std::exp(-r * T) * norm_cdf(-d2) - S * norm_cdf(-d1);
        return put;
    }
}

BinomialModel::BinomialModel(int steps)
    : steps_(steps) {}

double BinomialModel::price(const OptionSpec& opt) const {
    int N = std::max(1, steps_);
    double S0 = opt.spot;
    double K  = opt.strike;
    double r  = opt.rate;
    double sigma = opt.vol;
    double T  = opt.maturity;

    double dt = T / N;
    double u  = std::exp(sigma * std::sqrt(dt));
    double d  = 1.0 / u;
    double disc = std::exp(-r * dt);
    double p = (std::exp(r * dt) - d) / (u - d);

    std::vector<double> option_values(N + 1);

    for (int i = 0; i <= N; ++i) {
        int up_moves   = i;
        int down_moves = N - i;
        double ST = S0 * std::pow(u, up_moves) * std::pow(d, down_moves);
        double payoff = opt.is_call
            ? std::max(ST - K, 0.0)
            : std::max(K - ST, 0.0);
        option_values[i] = payoff;
    }

    for (int step = N - 1; step >= 0; --step) {
        for (int i = 0; i <= step; ++i) {
            double expected = p * option_values[i + 1] + (1.0 - p) * option_values[i];
            option_values[i] = disc * expected;
        }
    }

    return option_values[0];
}
