import time
import pricing_core as pc
from bs_python import price_bs_python


# set up option spec once
opt = pc.OptionSpec()
opt.spot = 100
opt.strike = 100
opt.rate = 0.01
opt.vol = 0.2
opt.maturity = 1.0
opt.is_call = True

bs_cpp = pc.BlackScholesModel()

N = 100_000  # enough iterations to get a stable timing

# ----- time pure Python -----
start = time.perf_counter()
for _ in range(N):
    price_bs_python(
        spot=opt.spot,
        strike=opt.strike,
        rate=opt.rate,
        vol=opt.vol,
        maturity=opt.maturity,
        is_call=opt.is_call,
    )
end = time.perf_counter()
python_time = end - start
print("Python time:", python_time)

# ----- time C++ (pybind) -----
start = time.perf_counter()
for _ in range(N):
    bs_cpp.price(opt)
end = time.perf_counter()
cpp_time = end - start
print("C++ time:", cpp_time)

speedup = python_time / cpp_time
print("Speedup ~", speedup, "x")
