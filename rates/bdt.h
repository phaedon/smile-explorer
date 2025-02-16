
#include <ceres/ceres.h>

#include "markets/binomial_tree.h"
#include "markets/rates/arrow_debreu.h"
#include "markets/rates/swaps.h"
#include "markets/time.h"

namespace markets {

struct BdtPropagator {
  BdtPropagator(int num_timesteps, double b, double spot_rate)
      : a_(num_timesteps, spot_rate), b_(b), num_timesteps_(num_timesteps) {}

  double operator()(const BinomialTree& tree, int t, int i) const {
    return a_[t] * std::exp(b_ * i);
  }

  void updateVol(double vol) { b_ = vol; }

  void updateA(int t, double val) { a_[t] = val; }
  std::vector<double> a_;

 private:
  double b_;
  int num_timesteps_;
};

struct BdtCalibrationCost {
  BdtCalibrationCost(BinomialTree& rate_tree,
                     BdtPropagator& bdt_prop,
                     BinomialTree& adtree,
                     const ArrowDebreauPropagator& arrowdeb_prop,
                     int t,
                     double observed_rate)
      : t_(t),
        observed_rate_(observed_rate),
        rate_tree_(rate_tree),
        adtree_(adtree),
        bdt_prop_(bdt_prop),
        arrowdeb_prop_(arrowdeb_prop) {}

  template <typename T>
  bool operator()(const T* const a_t, T* residual) const {
    // Use it to rebuild the BDT tree (short rate process).
    rate_tree_.forwardPropagate(bdt_prop_);

    // Recompute the discount factors.
    adtree_.forwardPropagate(arrowdeb_prop_);

    int num_years_to_maturity =
        std::ceil(t_ * adtree_.timestep().count() / 365.);

    double computed_rate = swapRate<Period::kAnnual>(
        adtree_, std::chrono::years(num_years_to_maturity));

    *residual = static_cast<T>(computed_rate) - static_cast<T>(observed_rate_);
    return true;
  }

  int t_;
  double observed_rate_;
  BinomialTree& rate_tree_;
  BinomialTree& adtree_;
  BdtPropagator& bdt_prop_;
  const ArrowDebreauPropagator& arrowdeb_prop_;
};

inline void calibrate(BinomialTree& rate_tree,
                      BdtPropagator& bdt_prop,
                      BinomialTree& adtree,
                      const ArrowDebreauPropagator& arrowdeb,
                      const std::vector<double>& yield_curve) {
  for (int t = 0;
       t < std::min((int)yield_curve.size(), rate_tree.numTimesteps());
       ++t) {
    ceres::Problem problem;

    const double observed_rate = yield_curve[t];
    double estimated_a_t = observed_rate;
    BdtCalibrationCost cost_function(
        rate_tree, bdt_prop, adtree, arrowdeb, t, observed_rate);

    problem.AddParameterBlock(&bdt_prop.a_[t], 1);

    auto autodiffcost =
        std::make_unique<ceres::AutoDiffCostFunction<BdtCalibrationCost, 1, 1>>(
            &cost_function);
    // Define the parameter block for a_[t]
    problem.AddResidualBlock(autodiffcost.get(),
                             nullptr,           // No loss function
                             &bdt_prop.a_[t]);  // The parameter to optimize

    ceres::Solver::Options options;
    options.linear_solver_type = ceres::DENSE_QR;  // Or other suitable solver
    options.minimizer_progress_to_stdout = true;   // Set to true for debugging.
    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary);
    // bdt_prop.updateA(t, estimated_a_t);
  }

  // std::cout << summary.BriefReport() << std::endl;  // For debugging
}

}  // namespace markets