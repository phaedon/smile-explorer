Work in progress to implement binomial tree visualisation for options, etc.
Supports MacOS and Linux.

To run the example and the tests:
```
bazel run markets:render_tree
bazel test markets:binomial_tree_test markets:volatility_test
```

Next steps:
- Don't pass volatility into the constructor of `StochasticTreeModel` but rather pass it each time to forwardPropagate (and this can take care of resizing the trees as needed).
- Does Derivative need to copy the binomial tree at construction time? It seems that what is 
necessary is the ability to subscribe to updates, so when the underlying Timegrid changes, then
the derivative is notified and the deriv_tree is rebuilt. It could do this for a portfolio of derivs.
And then maybe Derivative could just hold a pointer to the Asset so that it is always current.
