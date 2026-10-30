#pragma once 
#include<algorithm>
#include<cmath>
#include<limits>
#include<vector>
#include "types.hpp"



// Cox-Ross-Rubinstein recombining binomial tree, European and American.
// PRECONDITION: the caller has already run qa::check().
namespace qa::binomial
{
    [[nodiscard]] inline double price(const OptionSpec& o, int steps){
    const int n = std::max(1,steps);
    const double dt = o.maturity/n;
    const double u = std::exp(o.vol*std::sqrt(dt));
    const double d = 1.0/u;
    const double disc = std::exp(-o.rate*dt);
    const double p = (std::exp(o.rate*dt)-d)/(u-d);

    if( !(p>0.0 && p<1.0)){
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double pu = disc*p;
    const double pd = disc*(1.0-p);
    const double sign = o.is_call?1.0:-1.0;

    std::vector<double>spot(n+1),val(n+1);

    spot[0] = o.spot * std::pow(d,n);
    const double ratio = u*u;
    for(int i=1;i<=n;i++){
        spot[i] = spot[i-1]*ratio;
    }

    for(int i=0;i<=n;i++){
        val[i] = std::max(sign*(spot[i]-o.strike),0.0);
    }

    for(int step = n-1;step>=0;step--){
        for(int i=0;i<=step;i++){
            spot[i]*=u;
            const double cont = pu*val[i+1]+pd*val[i];
            val[i] = o.is_american?std::max(cont,sign*(spot[i]-o.strike)):cont;
        }
    }
    return val[0];
}
} // namespace qa::binomaial


