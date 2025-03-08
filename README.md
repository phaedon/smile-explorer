### Overview

This library was inspired by a review of binomial tree methods for pricing options and other derivatives. 

In particular the formulas and implementations are adapted from a close reading of 
* Emanuel Derman, **The Volatility Smile**. [Wiley Link](https://www.wiley.com/en-be/The+Volatility+Smile-p-9781118959169) 

and many of the unit tests are based on the examples and end-of-chapter exercises in the text.

### Getting started
Install [Bazel](https://bazel.build/install) and then run:
```shell
bazel run explorer
bazel test ...
```

This library works on MacOS and Linux. (Windows is not yet supported because of the use of GLFW, which was recently added to the BCR without Windows support. See the [Bazel build rule here](https://github.com/bazelbuild/bazel-central-registry/blob/main/modules/glfw/3.3.9/patches/add_build_file.patch) for context.)

### Screenshots

A simple European option, priced on a binomial tree with backward induction:

![OptionPricing](https://github.com/user-attachments/assets/f9156e6a-de24-42a4-a61c-7e6cd9073888)


Local volatility:

![LocalVol](https://github.com/user-attachments/assets/8307eccd-1fbe-4f92-bb79-2ccba2850e88)
