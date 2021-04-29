#include <gtest/gtest.h>
#include <torch/csrc/deploy/deploy.h>
#include <torch/script.h>
#include <torch/torch.h>
#include <future>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  int rc = RUN_ALL_TESTS();
  return rc;
}

void compare_torchpy_jit(const char* model_filename, const char* jit_filename) {
  // Test
  torch::deploy::InterpreterManager m(1);
  torch::deploy::Package p = m.load_package(model_filename);
  auto model = p.load_pickle("model", "model.pkl");
  at::IValue eg;
  {
    auto I = p.acquire_session();
    eg = I.self.attr("load_pickle")({"model", "example.pkl"}).toIValue();
  }

  at::Tensor output = model(eg.toTuple()->elements()).toTensor();

  // Reference
  auto ref_model = torch::jit::load(jit_filename);
  at::Tensor ref_output =
      ref_model.forward(eg.toTuple()->elements()).toTensor();

  ASSERT_TRUE(ref_output.allclose(output, 1e-03, 1e-05));
}

const char* simple = "torch/csrc/deploy/example/generated/simple";
const char* simple_jit = "torch/csrc/deploy/example/generated/simple_jit";

const char* path(const char* envname, const char* path) {
  const char* e = getenv(envname);
  return e ? e : path;
}

TEST(TorchpyTest, SimpleModel) {
  compare_torchpy_jit(path("SIMPLE", simple), path("SIMPLE_JIT", simple_jit));
}

TEST(TorchpyTest, ResNet) {
  compare_torchpy_jit(
      path("RESNET", "torch/csrc/deploy/example/generated/resnet"),
      path("RESNET_JIT", "torch/csrc/deploy/example/generated/resnet_jit"));
}

TEST(TorchpyTest, Movable) {
  torch::deploy::InterpreterManager m(1);
  torch::deploy::ReplicatedObj obj;
  {
    auto I = m.acquire_one();
    auto model =
        I.global("torch.nn", "Module")(std::vector<torch::deploy::Obj>());
    obj = I.create_movable(model);
  }
  obj.acquire_session();
}

TEST(TorchpyTest, MultiSerialSimpleModel) {
  torch::deploy::InterpreterManager manager(3);
  torch::deploy::Package p = manager.load_package(path("SIMPLE", simple));
  auto model = p.load_pickle("model", "model.pkl");
  auto ref_model = torch::jit::load(path("SIMPLE_JIT", simple_jit));

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
  auto input = torch::ones({10, 20});
  size_t ninterp = 3;
  std::vector<at::Tensor> outputs;

  for (size_t i = 0; i < ninterp; i++) {
    outputs.push_back(model({input}).toTensor());
  }

  // Generate reference
  auto ref_output = ref_model.forward({input}).toTensor();

  // Compare all to reference
  for (size_t i = 0; i < ninterp; i++) {
    ASSERT_TRUE(ref_output.equal(outputs[i]));
  }
}

TEST(TorchpyTest, ThreadedSimpleModel) {
  size_t nthreads = 3;
  torch::deploy::InterpreterManager manager(nthreads);

  torch::deploy::Package p = manager.load_package(path("SIMPLE", simple));
  auto model = p.load_pickle("model", "model.pkl");
  auto ref_model = torch::jit::load(path("SIMPLE_JIT", simple_jit));

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
  auto input = torch::ones({10, 20});

  std::vector<at::Tensor> outputs;

  std::vector<std::future<at::Tensor>> futures;
  for (size_t i = 0; i < nthreads; i++) {
    futures.push_back(std::async(std::launch::async, [&model]() {
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
      auto input = torch::ones({10, 20});
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
      for (int i = 0; i < 100; ++i) {
        model({input}).toTensor();
      }
      auto result = model({input}).toTensor();
      return result;
    }));
  }
  for (size_t i = 0; i < nthreads; i++) {
    outputs.push_back(futures[i].get());
  }

  // Generate reference
  auto ref_output = ref_model.forward({input}).toTensor();

  // Compare all to reference
  for (size_t i = 0; i < nthreads; i++) {
    ASSERT_TRUE(ref_output.equal(outputs[i]));
  }
}
