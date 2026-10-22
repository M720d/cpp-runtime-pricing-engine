import sys, time
sys.path.insert(0, "build")          # so `import pricing_core` finds build/*.so
import pricing_core as pc

opt = pc.OptionSpec()
opt.spot, opt.strike, opt.rate, opt.vol, opt.maturity = 100.0, 100.0, 0.03, 0.2, 1.0

def best_of(fn, reps=5):
    times = []
    for _ in range(reps):
        t = time.perf_counter()
        fn()
        times.append(time.perf_counter() - t)
    return min(times)            # min, not mean: noise only ever makes a run slower

binom = pc.BinomialModel(2000)
mc = pc.MonteCarloModel(paths=1_000_000, steps=1)


opt_am = pc.OptionSpec()
opt_am.spot, opt_am.strike, opt_am.rate, opt_am.vol, opt_am.maturity = 100.0, 100.0, 0.03, 0.2, 1.0
opt_am.is_call, opt_am.is_american = False, True      # American put

print(f"binomial AMERICAN put: {best_of(lambda: binom.price(opt_am))*1e3:8.2f} ms   price={binom.price(opt_am):.6f}")
print(f"binomial 2000 steps : {best_of(lambda: binom.price(opt))*1e3:8.2f} ms   price={binom.price(opt):.6f}")
print(f"monte carlo 1M paths: {best_of(lambda: mc.price(opt))*1e3:8.2f} ms   price={mc.price(opt):.6f}")
