Work in progress to implement binomial tree visualisation for options, etc.
Supports MacOS and Linux.

To run the example and the tests:
```
bazel run markets:render_tree
bazel test markets:binomial_tree_test markets:volatility_test
```
