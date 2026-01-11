import pricing_core as pc

opt = pc.OptionSpec()
opt.spot = 100
opt.strike = 100
opt.rate = 0.01
opt.vol = 0.2
opt.maturity = 1.0
opt.is_call = True

bs = pc.BlackScholesModel()
tree = pc.BinomialModel(200)

print("BS direct:", bs.price(opt))
print("Binomial direct:", tree.price(opt))

engine = pc.PricingEngine()
engine.set_shared_model(bs)
print("Engine with BS:", engine.price(opt))
engine.set_shared_model(tree)
print("Engine with Binomial:", engine.price(opt))
