#pragma once

#include <memory>

struct OptionSpec {
    double spot;
    double strike;
    double rate;
    double vol;
    double maturity;
    bool   is_call;
};

class PricingModel {
public:
    virtual double price(const OptionSpec& opt) const = 0;
    virtual ~PricingModel() = default;
};

class BlackScholesModel : public PricingModel {
public:
    double price(const OptionSpec& opt) const override;
};

class BinomialModel : public PricingModel {
    int steps_;
public:
    explicit BinomialModel(int steps);
    double price(const OptionSpec& opt) const override;
};

class PricingEngine {
    std::shared_ptr<PricingModel> model_;
public:
    PricingEngine() = default;

    void set_shared_model(std::shared_ptr<PricingModel> m) {
        model_ = std::move(m);
    }

    double price(const OptionSpec& opt) const {
        if (!model_) {
            throw std::runtime_error("Model not set");
        }
        return model_->price(opt);
    }
};
