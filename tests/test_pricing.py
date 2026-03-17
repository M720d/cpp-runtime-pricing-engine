# test_pricing.py — accuracy and behaviour tests for the pricing engine.
#
# Test structure:
#   TestBlackScholesPrice   — verifies BS prices against known textbook values
#   TestBinomialConvergence — verifies the binomial tree converges to BS,
#                             and that American >= European (early exercise premium)
#   TestBlackScholesGreeks  — verifies Greek signs, ranges, and analytic vs FD
#   TestMonteCarlo          — verifies MC converges to BS, seed reproducibility
#   TestValidation          — verifies bad inputs raise exceptions
#   TestPricingEngine       — verifies the engine facade: delegation, swap, no model
#
# Run with:
#   cd project_root && pytest tests/ -v

import pytest
import math
import sys
import os

# conftest.py already adds build/ to sys.path, but add it explicitly here
# so the file can also be run directly (python tests/test_pricing.py).
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "build"))

import pricing_core as pc


# ---------------------------------------------------------------------------
# Helper: build a valid OptionSpec with sensible ATM defaults.
# Tests override individual fields as needed.
# ---------------------------------------------------------------------------
def make_opt(spot=100.0, strike=100.0, rate=0.05, vol=0.20,
             maturity=1.0, is_call=True, is_american=False):
    opt = pc.OptionSpec()
    opt.spot      = spot
    opt.strike    = strike
    opt.rate      = rate
    opt.vol       = vol
    opt.maturity  = maturity
    opt.is_call   = is_call
    opt.is_american = is_american
    return opt


# ===========================================================================
# TestBlackScholesPrice
# ===========================================================================
# Verify that the Black-Scholes model returns correct prices.
# We use the canonical ATM case (S=K=100, r=5%, vol=20%, T=1y) whose
# closed-form value is well-known, and structural properties (put-call parity,
# deep ITM/OTM behaviour) that must hold for any correct BS implementation.
# ===========================================================================
class TestBlackScholesPrice:

    def setup_method(self):
        # Fresh model instance for each test.
        self.bs = pc.BlackScholesModel()

    def test_atm_call_known_value(self):
        """
        Canonical test: ATM call with S=100, K=100, r=5%, vol=20%, T=1y.
        The exact BS price is approximately 10.4506.
        We allow a tolerance of 0.001 (well within any sensible implementation).
        """
        opt = make_opt()
        price = self.bs.price(opt)
        assert abs(price - 10.4506) < 0.001, (
            f"ATM call price {price:.4f} deviates from known value 10.4506")

    def test_put_call_parity(self):
        """
        Put-call parity: C - P = S - K * exp(-rT)
        This is a model-independent identity; any BS implementation must satisfy it.
        If this test fails the pricing formula is fundamentally wrong.
        """
        call_opt = make_opt(is_call=True)
        put_opt  = make_opt(is_call=False)
        C = self.bs.price(call_opt)
        P = self.bs.price(put_opt)
        S = call_opt.spot
        K = call_opt.strike
        r = call_opt.rate
        T = call_opt.maturity
        parity = S - K * math.exp(-r * T)   # RHS of put-call parity
        assert abs((C - P) - parity) < 1e-10, (
            f"Put-call parity violated: C-P={C-P:.6f}, S-Ke^{{-rT}}={parity:.6f}")

    def test_deep_itm_call_approaches_intrinsic(self):
        """
        A very deep ITM call (S >> K) should be worth approximately S - K*exp(-rT)
        because the probability of finishing in the money approaches 1.
        """
        opt = make_opt(spot=200.0, strike=100.0)
        price = self.bs.price(opt)
        S, K, r, T = opt.spot, opt.strike, opt.rate, opt.maturity
        expected = S - K * math.exp(-r * T)   # ~104.88
        assert abs(price - expected) < 0.5, (
            f"Deep ITM call {price:.4f} far from intrinsic {expected:.4f}")

    def test_deep_otm_call_near_zero(self):
        """
        A very deep OTM call (K >> S) has near-zero probability of paying off
        and should be worth almost nothing.
        """
        opt = make_opt(spot=50.0, strike=200.0)
        price = self.bs.price(opt)
        assert price < 0.01, f"Deep OTM call should be near 0, got {price:.6f}"

    def test_put_price_positive(self):
        """Put prices must always be non-negative (no-arbitrage)."""
        opt = make_opt(is_call=False)
        assert self.bs.price(opt) > 0.0


# ===========================================================================
# TestBinomialConvergence
# ===========================================================================
# The binomial tree is a discrete approximation of BS — as steps increases
# the price must converge to the BS analytical price.  We also test the
# American early-exercise premium, which is the core new feature.
# ===========================================================================
class TestBinomialConvergence:

    def setup_method(self):
        self.bs = pc.BlackScholesModel()

    def _bs_price(self, **kwargs):
        return self.bs.price(make_opt(**kwargs))

    def test_european_converges_to_bs(self):
        """
        As steps grows, |Binomial - BS| must decrease.
        At 500 steps the error should be < 0.05 (well under 1 cent on a $10 option).
        """
        opt = make_opt()
        bs_price = self.bs.price(opt)
        prev_error = float('inf')
        for steps in [10, 50, 200, 500]:
            binom_price = pc.BinomialModel(steps).price(opt)
            error = abs(binom_price - bs_price)
            assert error < prev_error + 0.1, (  # allow small oscillation
                f"Binomial error did not improve at steps={steps}: error={error:.4f}")
            prev_error = error
        # Final convergence: 500 steps must be within 0.05 of BS.
        assert error < 0.05, (
            f"Binomial(500) error {error:.4f} too large vs BS {bs_price:.4f}")

    def test_american_put_ge_european_put(self):
        """
        American put >= European put always (early exercise premium is >= 0).
        The difference is most pronounced for deep ITM puts.
        """
        eu_opt = make_opt(is_call=False, spot=80.0, is_american=False)
        am_opt = make_opt(is_call=False, spot=80.0, is_american=True)
        binom = pc.BinomialModel(300)
        eu_price = binom.price(eu_opt)
        am_price = binom.price(am_opt)
        assert am_price >= eu_price - 1e-9, (
            f"American put {am_price:.4f} < European put {eu_price:.4f}")

    def test_american_call_equals_european_call_no_dividends(self):
        """
        Without dividends, it is never optimal to early-exercise a call.
        Therefore American call == European call (to within numerical noise).
        This is a classical result in options theory.
        """
        eu_opt = make_opt(is_call=True, is_american=False)
        am_opt = make_opt(is_call=True, is_american=True)
        binom = pc.BinomialModel(300)
        eu_price = binom.price(eu_opt)
        am_price = binom.price(am_opt)
        assert abs(am_price - eu_price) < 1e-6, (
            f"American call {am_price:.6f} != European call {eu_price:.6f} "
            f"(should be equal without dividends)")

    def test_american_put_exceeds_european_significantly(self):
        """
        For a deep ITM put the early exercise premium should be material
        (measurably greater than zero, not just floating-point noise).
        """
        eu_opt = make_opt(is_call=False, spot=70.0, rate=0.05, is_american=False)
        am_opt = make_opt(is_call=False, spot=70.0, rate=0.05, is_american=True)
        binom = pc.BinomialModel(300)
        premium = binom.price(am_opt) - binom.price(eu_opt)
        assert premium > 0.01, (
            f"Early exercise premium {premium:.4f} should be positive for deep ITM put")


# ===========================================================================
# TestBlackScholesGreeks
# ===========================================================================
# Verify that the analytic Greeks have the correct sign and range, and that
# they match finite-difference estimates to high precision.
# ===========================================================================
class TestBlackScholesGreeks:

    def setup_method(self):
        self.bs  = pc.BlackScholesModel()
        self.opt = make_opt()  # ATM call

    def test_call_delta_in_range(self):
        """Call delta must be strictly between 0 and 1 (bounded probability)."""
        g = self.bs.greeks(self.opt)
        assert 0.0 < g.delta < 1.0, f"Call delta {g.delta:.4f} out of (0,1)"

    def test_put_delta_in_range(self):
        """Put delta must be strictly between -1 and 0."""
        put_opt = make_opt(is_call=False)
        g = self.bs.greeks(put_opt)
        assert -1.0 < g.delta < 0.0, f"Put delta {g.delta:.4f} out of (-1,0)"

    def test_atm_call_delta_near_half(self):
        """
        ATM call delta is slightly above 0.5 (due to the (r + vol²/2)*T drift term
        in d1).  For our parameters (r=5%, vol=20%, T=1) it should be ~0.63.
        """
        g = self.bs.greeks(self.opt)
        assert 0.5 < g.delta < 0.8, f"ATM call delta {g.delta:.4f} unexpected"

    def test_gamma_always_positive(self):
        """
        Gamma is always positive for long options (calls and puts).
        Positive gamma means the position benefits from large moves in either direction.
        """
        g_call = self.bs.greeks(make_opt(is_call=True))
        g_put  = self.bs.greeks(make_opt(is_call=False))
        assert g_call.gamma > 0.0, f"Call gamma {g_call.gamma} should be positive"
        assert g_put.gamma  > 0.0, f"Put gamma {g_put.gamma} should be positive"

    def test_vega_always_positive(self):
        """
        Vega is always positive for long vanilla options: higher vol => higher premium.
        True for both calls and puts.
        """
        g_call = self.bs.greeks(make_opt(is_call=True))
        g_put  = self.bs.greeks(make_opt(is_call=False))
        assert g_call.vega > 0.0, f"Call vega {g_call.vega} should be positive"
        assert g_put.vega  > 0.0, f"Put vega {g_put.vega} should be positive"

    def test_call_theta_negative(self):
        """
        Theta is negative for long options: time passing reduces the option's value
        (time decay).  Theta > 0 means the option loses value as T shrinks.
        Wait — we defined theta = -dP/dT in our struct.
        Since dP/dT > 0 (more time = more value), theta = -dP/dT > 0... let's verify.
        """
        g = self.bs.greeks(self.opt)
        # theta = -(dP/dT), and dP/dT > 0 (more time = higher price)
        # so theta should be NEGATIVE in this convention.
        # (Note: different textbooks use opposite sign conventions.)
        assert g.theta < 0.0, (
            f"Call theta {g.theta:.4f} should be negative "
            "(option loses value as time passes)")

    def test_call_rho_positive(self):
        """Call rho > 0: higher risk-free rate raises call value (put-call parity)."""
        g = self.bs.greeks(self.opt)
        assert g.rho > 0.0, f"Call rho {g.rho:.4f} should be positive"

    def test_put_rho_negative(self):
        """Put rho < 0: higher risk-free rate lowers put value."""
        g = self.bs.greeks(make_opt(is_call=False))
        assert g.rho < 0.0, f"Put rho {g.rho:.4f} should be negative"

    def test_analytic_delta_matches_finite_diff(self):
        """
        The analytic BS delta should match a manual finite-difference bump
        to within 1e-5.  This confirms the analytic formula is correctly derived.
        """
        h = 0.01  # $0.01 spot bump
        opt  = self.opt
        up   = make_opt(spot=opt.spot + h)
        dn   = make_opt(spot=opt.spot - h)
        fd_delta = (self.bs.price(up) - self.bs.price(dn)) / (2.0 * h)
        analytic_delta = self.bs.greeks(opt).delta
        assert abs(analytic_delta - fd_delta) < 1e-5, (
            f"Analytic delta {analytic_delta:.6f} != FD delta {fd_delta:.6f}")

    def test_analytic_vega_matches_finite_diff(self):
        """Similar cross-check for vega."""
        h = 0.001  # 0.1% vol bump
        opt = self.opt
        up  = make_opt(vol=opt.vol + h)
        dn  = make_opt(vol=opt.vol - h)
        fd_vega = (self.bs.price(up) - self.bs.price(dn)) / (2.0 * h)
        analytic_vega = self.bs.greeks(opt).vega
        assert abs(analytic_vega - fd_vega) < 0.01, (
            f"Analytic vega {analytic_vega:.4f} != FD vega {fd_vega:.4f}")


# ===========================================================================
# TestMonteCarlo
# ===========================================================================
# Verify that the Monte Carlo model:
#   1. Converges to the BS price for large path counts
#   2. Is reproducible (same seed = same price)
#   3. Gives different prices for different seeds
#   4. Rejects American options with a clear error
# ===========================================================================
class TestMonteCarlo:

    def setup_method(self):
        self.bs  = pc.BlackScholesModel()
        self.opt = make_opt()  # ATM European call

    def test_european_call_converges_to_bs(self):
        """
        With 200k paths the MC price should be within 1% of the BS price.
        The MC standard error for this case is ~sigma/sqrt(N) ≈ a few cents.
        Using a fixed seed avoids flaky test failures from unlucky draws.
        """
        bs_price = self.bs.price(self.opt)
        mc = pc.MonteCarloModel(paths=200000, steps=252, seed=42)
        mc_price = mc.price(self.opt)
        rel_error = abs(mc_price - bs_price) / bs_price
        assert rel_error < 0.01, (
            f"MC price {mc_price:.4f} deviates from BS {bs_price:.4f} "
            f"by {rel_error*100:.2f}% (limit 1%)")

    def test_same_seed_gives_same_price(self):
        """Two models with identical seeds must return bit-for-bit identical prices."""
        mc1 = pc.MonteCarloModel(paths=10000, seed=99)
        mc2 = pc.MonteCarloModel(paths=10000, seed=99)
        assert mc1.price(self.opt) == mc2.price(self.opt), (
            "Same seed produced different prices — RNG is not deterministic")

    def test_different_seeds_give_different_prices(self):
        """Two models with different seeds should give different (if close) prices."""
        mc1 = pc.MonteCarloModel(paths=10000, seed=1)
        mc2 = pc.MonteCarloModel(paths=10000, seed=2)
        assert mc1.price(self.opt) != mc2.price(self.opt), (
            "Different seeds produced identical prices — something is wrong with RNG")

    def test_more_paths_reduces_variance(self):
        """
        Higher path count should give a price closer to BS.
        We check that 50k paths is more accurate than 1k paths.
        """
        bs_price = self.bs.price(self.opt)
        err_small = abs(pc.MonteCarloModel(paths=1000,  seed=42).price(self.opt) - bs_price)
        err_large = abs(pc.MonteCarloModel(paths=50000, seed=42).price(self.opt) - bs_price)
        assert err_large < err_small, (
            f"More paths did not improve accuracy: err(1k)={err_small:.4f}, "
            f"err(50k)={err_large:.4f}")

    def test_american_option_raises(self):
        """
        American MC pricing is not yet implemented.
        The model must raise an exception rather than returning a wrong answer.
        """
        am_opt = make_opt(is_american=True)
        mc = pc.MonteCarloModel()
        with pytest.raises(Exception):
            mc.price(am_opt)

    def test_put_price_positive(self):
        """MC put prices should be positive."""
        put_opt = make_opt(is_call=False)
        mc = pc.MonteCarloModel(paths=100000, seed=42)
        assert mc.price(put_opt) > 0.0


# ===========================================================================
# TestValidation
# ===========================================================================
# Verify that PricingEngine rejects bad inputs before they reach the model.
# We use parametrize to test each bad-field combination in one test function.
# ===========================================================================
class TestValidation:

    def setup_method(self):
        self.engine = pc.PricingEngine()
        self.engine.set_shared_model(pc.BlackScholesModel())

    @pytest.mark.parametrize("field, value", [
        ("spot",     -1.0),    # negative spot
        ("spot",      0.0),    # zero spot
        ("strike",   -1.0),    # negative strike
        ("strike",    0.0),    # zero strike
        ("vol",      -0.1),    # negative volatility
        ("vol",       0.0),    # zero volatility
        ("maturity", -1.0),    # negative maturity
        ("maturity",  0.0),    # zero maturity (expired)
    ])
    def test_invalid_field_raises(self, field, value):
        """
        Each bad input field should cause the engine to raise an exception
        (std::invalid_argument from validate_option_spec, mapped to Python ValueError).
        """
        opt = make_opt()          # start with a fully valid spec
        setattr(opt, field, value)  # corrupt one field
        with pytest.raises(Exception):
            self.engine.price(opt)


# ===========================================================================
# TestPricingEngine
# ===========================================================================
# Verify the PricingEngine facade: correct delegation to models, runtime
# model swapping, and proper error when no model is set.
# ===========================================================================
class TestPricingEngine:

    def setup_method(self):
        self.opt = make_opt()

    def test_price_matches_direct_model_call(self):
        """
        Engine.price(opt) must return the same result as BlackScholesModel().price(opt)
        — it is just a delegating facade, so no values should change.
        """
        engine = pc.PricingEngine()
        engine.set_shared_model(pc.BlackScholesModel())
        direct = pc.BlackScholesModel().price(self.opt)
        assert engine.price(self.opt) == direct, (
            "Engine.price != direct BlackScholesModel.price")

    def test_greeks_delegation_returns_greeks_result(self):
        """
        Engine.greeks(opt) must return a GreeksResult with float fields.
        This verifies the delegation chain (engine -> model -> greeks).
        """
        engine = pc.PricingEngine()
        engine.set_shared_model(pc.BlackScholesModel())
        g = engine.greeks(self.opt)
        assert isinstance(g.delta, float), "delta should be a float"
        assert isinstance(g.gamma, float), "gamma should be a float"

    def test_no_model_set_raises_on_price(self):
        """
        Calling price() before set_shared_model() should raise RuntimeError,
        not segfault or silently return garbage.
        """
        engine = pc.PricingEngine()  # no model set
        with pytest.raises(Exception):
            engine.price(self.opt)

    def test_no_model_set_raises_on_greeks(self):
        """Same guard must apply to greeks()."""
        engine = pc.PricingEngine()
        with pytest.raises(Exception):
            engine.greeks(self.opt)

    def test_runtime_model_swap(self):
        """
        Models can be swapped at runtime without recreating the engine.
        BS and Binomial(500) should give close but not identical prices.
        """
        engine = pc.PricingEngine()

        engine.set_shared_model(pc.BlackScholesModel())
        bs_price = engine.price(self.opt)

        engine.set_shared_model(pc.BinomialModel(500))
        binom_price = engine.price(self.opt)

        # They should be close (same underlying model family, 500 steps is accurate)
        assert abs(bs_price - binom_price) < 0.05, (
            f"BS {bs_price:.4f} and Binomial(500) {binom_price:.4f} too far apart")

    def test_model_swap_to_monte_carlo(self):
        """
        Swap from BS to MC and verify the MC price is in a reasonable range.
        """
        engine = pc.PricingEngine()
        engine.set_shared_model(pc.BlackScholesModel())
        bs_price = engine.price(self.opt)

        engine.set_shared_model(pc.MonteCarloModel(paths=200000, seed=42))
        mc_price = engine.price(self.opt)

        assert abs(bs_price - mc_price) / bs_price < 0.02, (
            f"After swapping to MC, price {mc_price:.4f} too far from BS {bs_price:.4f}")
