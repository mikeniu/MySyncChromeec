# Zephyr Testing

[TOC]

Before starting, developers should read the following:
- [Zephyr Test Framework](https://docs.zephyrproject.org/latest/develop/test/ztest.html)
- [Zephyr Test Runner (Twister)](https://docs.zephyrproject.org/latest/develop/test/twister.html)

## Getting help

Get your tests reviewed by adding [zephyr-test-eng@google.com](mailto:zephyr-test-eng@google.com)
as a reviewer on your CL.

Ask a question:
- If you're a googler, please post using our YAQS label [zephyr-rtos-test](http://yaqs/eng/t/zephyr-rtos-test)
- External contributors should email to [zephyr-test-eng@google.com](mailto:zephyr-test-eng@google.com)
  or ask on the public Zephyr discord [#testing](https://discord.com/channels/720317445772017664/733037944922964069)
  channel for generic Zephyr questions.

## Where to add tests?

Tests currently reside in [zephyr/test](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/test/).
When adding a new test it's possible to either create a new directory which
includes a `testcase.yaml` or add a new entry to an existing `testcase.yaml` if
the test is a slight variation of an existing test.

## The structure of the `testcase.yaml`

The top level test yaml file may optionally contain a `common` entry which will
be applied to every entry under `tests`. Every attribute that can reside under a
test node, may reside under the `common` node.

Some common attributes include:
- `extra_configs` which is a list of Kconfigs to add to the test.
- `extra_args` which is a string containing additional arguments to pass to
  CMake

## Integration tests

Integration tests build the full EC. They require devicetree and all Kconfigs to
be set appropriately. To build an integration test, simply use the following
CMakeLists.txt template:

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HITS $ENV{ZEPHYR_BASE})
project(<your_test_project_name>)
target_sources(app
  PRIVATE
    src/...
)
```

Then, you'll need a default `prj.conf` file to set the default configs. Keep in
mind that this file will be automatically included in all the tests unless it is
overridden by the test's `extra_args` so you only want to put things that will
apply to all tests here. It will look like:

```
CONFIG_ZTEST=y
CONFIG_ZTEST_NEW_API=y

# Enable the shim of the legacy CrosEC (You’ll need both)
CONFIG_CROS_EC=y
CONFIG_PLATFORM_EC=y

# Enable the CONFIG_MATH_UTIL in the CrosEC
```

## Running twister

Run all tests under a specific directory:

```shell
platform/ec$ ./twister -T path/to/my/tests 
```

Run a specific test:
```shell
platform/ec$ ./twister -s path/to/my/tests/my.test.case
```

Run all tests with coverage:
```shell
platform/ec$ ./twister -p native_posix -p unit_testing --coverage -T .
```

Get more info on twister:
```shell
platform/ec$ ./twister --help
```

Other useful flags:
- `-i` Print inline logs to STDOUT on error (as well as long file)
- `-n` No clean (incremental builds)
- `-c` Clobber, don't create a new `twister-out/` directory
- `-v` Verbose logging (can use `-vv` or `-vvv` for more logging)
- `-b` Build only

## Using assumptions

The `zassume_*` API is used to minimize failures while allowing us to find out
exactly what's going wrong. When writing a test, only assert on code you're
testing. Any dependencies should use the `zassume_*` API. If the assumption
fails, your test will be marked as skipped and Twister will report an error.
Generally speaking, if an assumption fails, either the test wasn't set up
correctly, or there should be another test that's testing the dependency. 

## Debugging

If the test targets `native_posix` platforms, you can run it through your choice
of debugger. Additionally, it's possible to run only some of the tests via:

```shell
$ ./twister-out/native_posix/zephyr/test/.../zephyr/zephyr.exe -list
# List of all tests in the binary in the format of <suite_name:test_name>
$ ./twister-out/native_posix/zephyr/test/.../zephyr/zephyr.exe -test=suite0:*,suite1:test0
# Runs all tests under suite0 and test0 from suite1
```

## Deflaking

zTest allows deflaking tests via shuffling. To enable this feature, simply turn
on `CONFIG_ZTEST_SHUFFLE=y`. Your test binary's test suites and test order will
be random as well as run multiple times. To fine-tune the execution:
- Set `CONFIG_ZTEST_SHUFFLE_SUITE_REPEAT_COUNT` to an `int` controlling how many
  times the test suites will be shuffled and run.
- Set `CONFIG_ZTEST_SHUFFLE_TEST_REPEAT_COUNT` to an `int` controlling how many
  times the individual tests will be shuffled and run (per suite run).

Note that the total number of times that tests will run is the result of
multiplying those two Kconfig values.
