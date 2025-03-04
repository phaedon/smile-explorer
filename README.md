Work in progress to implement binomial tree visualisation for options, etc.
Supports MacOS and Linux.

To run the example and the tests:
```
bazel run markets:render_tree
bazel test markets:all
```

Next steps:
- Rethink design for RatesCurve to make it default-copyable and allow Derivative to store it
  as a reference. So that pricing automatically gets updates. The use of std::variant is clumsy.
- Implement local volatility.

