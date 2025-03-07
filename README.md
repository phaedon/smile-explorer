Work in progress to implement binomial tree visualisation for options, etc.
Supports MacOS and Linux. (Windows is not yet supported because of the use of GLFW, see the [Bazel build rule here](https://github.com/bazelbuild/bazel-central-registry/blob/main/modules/glfw/3.3.9/patches/add_build_file.patch) for reference.)

To run the example and the tests:
```
bazel run markets:render_tree
bazel test markets:all
```
