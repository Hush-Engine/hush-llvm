//===-- HushReflectionMatcherTest.cpp - Unit tests for HushReflectionMatcher ==//
//
// Tests the public utility methods of HushReflectionCallback.
// No full run() integration — tests only the helper predicates and
// initial callback state.
//
//===----------------------------------------------------------------------===//

#include "HushReflectionMatcher.h"
#include "gtest/gtest.h"

TEST(HushReflectionMatcherTest, FileInCWD) {
  HushReflectionCallback Callback("/home/user/project");
  EXPECT_TRUE(Callback.isFileInCurrentWorkingDirectory(
      "/home/user/project/src/Foo.hpp"));
}

TEST(HushReflectionMatcherTest, FileOutsideCWD) {
  HushReflectionCallback Callback("/home/user/project");
  EXPECT_FALSE(
      Callback.isFileInCurrentWorkingDirectory("/usr/include/string.h"));
}

TEST(HushReflectionMatcherTest, EmptyCallbackState) {
  HushReflectionCallback Callback("/tmp");
  EXPECT_TRUE(Callback.getReflectedTypes().empty());
  EXPECT_FALSE(Callback.hasFileBeenParsed("/tmp/Foo.hpp"));
}
