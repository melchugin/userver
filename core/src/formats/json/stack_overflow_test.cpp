#include <string>

#include <gtest/gtest.h>

#include <userver/engine/run_standalone.hpp>
#include <userver/formats/json/serialize.hpp>

USERVER_NAMESPACE_BEGIN

namespace {

std::string MakeStringOfDeepObject(std::size_t depth) {
  std::string str;
  str.reserve(depth * 6 + 1);
  for (std::size_t i = 0; i < depth; ++i) {
    str += R"({"a":)";
  }
  str += "1";
  for (std::size_t i = 0; i < depth; ++i) {
    str += "}";
  }
  return str;
}

std::string MakeStringOfDeepArray(std::size_t depth) {
  std::string str;
  str.reserve(2 * depth + 1);
  for (std::size_t i = 0; i < depth; ++i) {
    str += "[";
  }
  str += "1";
  for (std::size_t i = 0; i < depth; ++i) {
    str += "]";
  }
  return str;
}

}  // namespace

TEST(FormatsJson, DeepObjectFromString) {
  constexpr std::size_t kWorkerThreads = 1;
  engine::TaskProcessorPoolsConfig config;
  config.coro_stack_size = 32 * 1024ULL;

  engine::RunStandalone(kWorkerThreads, config, [] {
    constexpr std::size_t kDepth = 16000;
    auto value = formats::json::FromString(MakeStringOfDeepObject(kDepth));

    for (std::size_t i = 0; i < kDepth; ++i) {
      value = value["a"];
    }
    EXPECT_EQ(value.As<int>(), 1);
  });
}

TEST(FormatsJson, DeepArrayFromString) {
  constexpr std::size_t kWorkerThreads = 1;
  engine::TaskProcessorPoolsConfig config;
  config.coro_stack_size = 32 * 1024ULL;

  engine::RunStandalone(kWorkerThreads, config, [] {
    constexpr std::size_t kDepth = 16000;
    auto value = formats::json::FromString(MakeStringOfDeepArray(kDepth));

    for (std::size_t i = 0; i < kDepth; ++i) {
      value = value[0];
    }
    EXPECT_EQ(value.As<int>(), 1);
  });
}

USERVER_NAMESPACE_END
