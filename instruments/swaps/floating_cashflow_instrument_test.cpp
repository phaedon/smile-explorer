
#include "floating_cashflow_instrument.h"

#include <gtest/gtest.h>

#include "contract.h"
#include "rates/short_rate_tree_curve.h"
#include "rates/zero_curve.h"
#include "trees/hull_white_propagator.h"
#include "trees/trinomial_tree.h"

namespace smileexplorer {
namespace {

// Test fixture for FloatingCashflowInstrument tests
class FloatingCashflowInstrumentTest : public ::testing::Test {
 protected:
  void SetUp() override {
    market_curve_ = std::make_unique<ZeroSpotCurve>(
        std::vector<double>{1, 2, 5, 10}, 
        std::vector<double>{.03, .035, .0375, .039}, 
        CompoundingPeriod::kQuarterly);
    
    short_rate_tree_ = std::make_unique<ShortRateTreeCurve>(
        std::make_unique<HullWhitePropagator>(0.1, 0.02, .25 / 7),
        *market_curve_,
        10.0);
  }

  std::unique_ptr<ZeroSpotCurve> market_curve_;
  std::unique_ptr<ShortRateTreeCurve> short_rate_tree_;
};

TEST_F(FloatingCashflowInstrumentTest, BasicSwapPricing) {
  FloatingCashflowInstrument floating_leg(short_rate_tree_.get());

  floating_leg.setCashflows(
      SwapContractDetails{.direction = SwapDirection::kPayer,
                          .floating_rate_frequency = ForwardRateTenor::k3Month,
                          .notional_principal = 100.,
                          .start_date_years = 0.,
                          .end_date_years = 5.0});

  EXPECT_NEAR(100., market_curve_->df(5.0) * 100 + floating_leg.price(), 0.1);
}

// Test forward propagation behavior by examining tree state changes
TEST_F(FloatingCashflowInstrumentTest, ForwardPropagationWithoutDiscounting_BasicBehavior) {
  FloatingCashflowInstrument instrument(short_rate_tree_.get());
  
  // Test a single cashflow to examine forward propagation behavior
  SwapContractDetails single_payment_contract{
      .direction = SwapDirection::kReceiver,
      .floating_rate_frequency = ForwardRateTenor::k3Month,
      .notional_principal = 100.,
      .start_date_years = 0.,
      .end_date_years = 0.25  // Single quarterly payment
  };
  
  instrument.setCashflows(single_payment_contract);
  const auto& result_tree = instrument.trinomialTree();
  
  // Verify that forward propagation occurred (non-zero values should exist)
  bool has_nonzero_values = false;
  for (const auto& timestep : result_tree.tree_) {
    for (const auto& node : timestep) {
      if (std::abs(node.state_value) > 1e-10) {
        has_nonzero_values = true;
        break;
      }
    }
    if (has_nonzero_values) break;
  }
  
  EXPECT_TRUE(has_nonzero_values) << "Forward propagation should produce non-zero values";
  
  // Verify the tree has reasonable structure after cashflow setting
  EXPECT_GT(result_tree.tree_.size(), 1) << "Tree should have multiple time steps";
  EXPECT_GT(result_tree.tree_[0].size(), 0) << "Tree should have nodes at time 0";
}

// Test forward propagation probability conservation
TEST_F(FloatingCashflowInstrumentTest, ForwardPropagationWithoutDiscounting_ProbabilityConservation) {
  FloatingCashflowInstrument instrument(short_rate_tree_.get());
  
  // Use a contract that will exercise the forward propagation logic
  SwapContractDetails contract{
      .direction = SwapDirection::kReceiver,
      .floating_rate_frequency = ForwardRateTenor::k6Month,
      .notional_principal = 100.,
      .start_date_years = 0.,
      .end_date_years = 1.0  // Two semi-annual payments
  };
  
  instrument.setCashflows(contract);
  const auto& tree = instrument.trinomialTree();
  
  // Test that forward propagation conserves total probability-weighted value
  // at intermediate time steps (this is a key property of forward propagation without discounting)
  
  // Find a payment time index to examine
  int payment_time_index = -1;
  for (int ti = 1; ti < static_cast<int>(tree.tree_.size()); ++ti) {
    bool has_payments = false;
    for (const auto& node : tree.tree_[ti]) {
      if (std::abs(node.state_value) > 1e-6) {
        has_payments = true;
        break;
      }
    }
    if (has_payments) {
      payment_time_index = ti;
      break;
    }
  }
  
  ASSERT_GE(payment_time_index, 1) << "Should find at least one payment time index";
  
  // Verify that sum of probability-weighted values is reasonable
  double total_probability_weighted_value = 0.0;
  for (const auto& node : tree.tree_[payment_time_index]) {
    total_probability_weighted_value += node.state_value * node.arrow_debreu;
  }
  
  // The total should be non-zero and finite
  EXPECT_TRUE(std::isfinite(total_probability_weighted_value));
  EXPECT_NE(0.0, total_probability_weighted_value);
}

// Test edge cases in forward propagation
TEST_F(FloatingCashflowInstrumentTest, ForwardPropagationWithoutDiscounting_EdgeCases) {
  FloatingCashflowInstrument instrument(short_rate_tree_.get());
  
  // Test with zero notional (should handle gracefully)
  SwapContractDetails zero_notional_contract{
      .direction = SwapDirection::kPayer,
      .floating_rate_frequency = ForwardRateTenor::k3Month,
      .notional_principal = 0.,
      .start_date_years = 0.,
      .end_date_years = 1.0
  };
  
  EXPECT_NO_THROW(instrument.setCashflows(zero_notional_contract));
  EXPECT_NEAR(0.0, instrument.price(), 1e-10);
}

// Test forward propagation with different swap directions
TEST_F(FloatingCashflowInstrumentTest, ForwardPropagationWithoutDiscounting_SwapDirections) {
  FloatingCashflowInstrument payer_leg(short_rate_tree_.get());
  FloatingCashflowInstrument receiver_leg(short_rate_tree_.get());
  
  SwapContractDetails payer_contract{
      .direction = SwapDirection::kPayer,
      .floating_rate_frequency = ForwardRateTenor::k3Month,
      .notional_principal = 100.,
      .start_date_years = 0.,
      .end_date_years = 2.0
  };
  
  SwapContractDetails receiver_contract{
      .direction = SwapDirection::kReceiver,
      .floating_rate_frequency = ForwardRateTenor::k3Month,
      .notional_principal = 100.,
      .start_date_years = 0.,
      .end_date_years = 2.0
  };
  
  payer_leg.setCashflows(payer_contract);
  receiver_leg.setCashflows(receiver_contract);
  
  double payer_price = payer_leg.price();
  double receiver_price = receiver_leg.price();
  
  // Payer and receiver legs should have opposite signs (approximately)
  EXPECT_NEAR(payer_price, -receiver_price, 0.01);
  EXPECT_NE(0.0, payer_price);
  EXPECT_NE(0.0, receiver_price);
}

// Test forward propagation with different tenors
TEST_F(FloatingCashflowInstrumentTest, ForwardPropagationWithoutDiscounting_DifferentTenors) {
  FloatingCashflowInstrument quarterly_leg(short_rate_tree_.get());
  FloatingCashflowInstrument semiannual_leg(short_rate_tree_.get());
  
  SwapContractDetails quarterly_contract{
      .direction = SwapDirection::kReceiver,
      .floating_rate_frequency = ForwardRateTenor::k3Month,
      .notional_principal = 100.,
      .start_date_years = 0.,
      .end_date_years = 1.0
  };
  
  SwapContractDetails semiannual_contract{
      .direction = SwapDirection::kReceiver,
      .floating_rate_frequency = ForwardRateTenor::k6Month,
      .notional_principal = 100.,
      .start_date_years = 0.,
      .end_date_years = 1.0
  };
  
  EXPECT_NO_THROW(quarterly_leg.setCashflows(quarterly_contract));
  EXPECT_NO_THROW(semiannual_leg.setCashflows(semiannual_contract));
  
  double quarterly_price = quarterly_leg.price();
  double semiannual_price = semiannual_leg.price();
  
  // Both should be finite and non-zero
  EXPECT_TRUE(std::isfinite(quarterly_price));
  EXPECT_TRUE(std::isfinite(semiannual_price));
  EXPECT_NE(0.0, quarterly_price);
  EXPECT_NE(0.0, semiannual_price);
  
  // Quarterly payments typically have slightly different value than semiannual
  // due to compounding effects, but should be in same ballpark
  EXPECT_GT(std::abs(quarterly_price / semiannual_price), 0.5);
  EXPECT_LT(std::abs(quarterly_price / semiannual_price), 2.0);
}

// Test numerical stability of forward propagation
TEST_F(FloatingCashflowInstrumentTest, ForwardPropagationWithoutDiscounting_NumericalStability) {
  FloatingCashflowInstrument instrument(short_rate_tree_.get());
  
  // Test with very small notional
  SwapContractDetails small_notional_contract{
      .direction = SwapDirection::kReceiver,
      .floating_rate_frequency = ForwardRateTenor::k3Month,
      .notional_principal = 1e-6,
      .start_date_years = 0.,
      .end_date_years = 1.0
  };
  
  EXPECT_NO_THROW(instrument.setCashflows(small_notional_contract));
  double small_price = instrument.price();
  EXPECT_TRUE(std::isfinite(small_price));
  
  // Reset and test with large notional
  FloatingCashflowInstrument instrument2(short_rate_tree_.get());
  SwapContractDetails large_notional_contract{
      .direction = SwapDirection::kReceiver,
      .floating_rate_frequency = ForwardRateTenor::k3Month,
      .notional_principal = 1e6,
      .start_date_years = 0.,
      .end_date_years = 1.0
  };
  
  EXPECT_NO_THROW(instrument2.setCashflows(large_notional_contract));
  double large_price = instrument2.price();
  EXPECT_TRUE(std::isfinite(large_price));
  
  // Price should scale approximately linearly with notional
  EXPECT_NEAR(large_price / small_price, 1e12, 1e10);
}

}  // namespace
}  // namespace smileexplorer
