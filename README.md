Work in progress to implement binomial tree visualisation for options, etc.
Supports MacOS and Linux.

To run the example and the tests:
```
bazel run markets:render_tree
bazel test markets:all
```

Next steps:
- Rethink design for `RatesCurve` to make it default-copyable and allow `Derivative` to store it
  as a reference. So that pricing automatically gets updates. The use of `std::variant` is clumsy.
- In addition, it really got me tripped up that the rates curve is optionally a monostate and has to
  get passed to both a propagator (potentially, because of local vol) and also to the underlying deriv
  and if you forget to do both, because the monostate is supported as the default arg, then it's toxic.
- Similarly, store a pointer or something to the asset from the deriv. I shouldn't have to pass its
  tree to the constructor and then every time I call `price()`.
