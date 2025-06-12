#include "prototype.h"

#include <gtest/gtest.h>

namespace smileexplorer {
namespace {

TEST(PrototypeTest, Foo) {
  auto path = generateUniformRandomPath(100);
  EXPECT_TRUE(true);
}

}  // namespace
}  // namespace smileexplorer
