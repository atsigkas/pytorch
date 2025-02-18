#include <torch/script.h>
#include <gtest/gtest.h>
#include <test/cpp/api/support.h>

using namespace torch::autograd;
using namespace torch::test;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
TEST(GradModeTest, TestRequiresGradFunctionalOp) {
  torch::AutoGradMode mode(false);
  for (bool requires_grad : {true, false}) {
    torch::Tensor c = torch::ones({1, 2, 3}).set_requires_grad(requires_grad);

    torch::Tensor func_out = c * c;
    ASSERT_FALSE(func_out.requires_grad());
    ASSERT_TRUE(func_out.is_leaf());
  }
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
TEST(GradModeTest, TestRequiresGradInplaceOp) {
  torch::AutoGradMode mode(false);
  for (bool requires_grad : {true, false}) {
    torch::Tensor c = torch::ones({1, 2, 3}).set_requires_grad(requires_grad);

    c.mul_(2);
    ASSERT_EQ(c.requires_grad(), requires_grad);
  }
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
TEST(GradModeTest, TestRequiresGradViewOp) {
  torch::AutoGradMode mode(false);
  for (bool requires_grad : {true, false}) {
    torch::Tensor c = torch::ones({1, 2, 3}).set_requires_grad(requires_grad);

    torch::Tensor view_out = c.view({2, 3});
    ASSERT_EQ(view_out.requires_grad(), requires_grad);
    ASSERT_TRUE(view_out.is_leaf());
  }
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
TEST(GradModeTest, TestRequiresGradViewOpExiting) {
  for (bool requires_grad: {true, false}) {
    torch::Tensor s = torch::ones({1, 2, 3}).set_requires_grad(requires_grad);
    torch::Tensor a = s.clone();
    torch::Tensor view_out, tmp;

    {
      torch::AutoGradMode mode(false);
      view_out = a.view({2, 3});  // go through kernels: VariableType, ADInplaceOrView, CPU
      assert_tensor_creation_meta(view_out, torch::autograd::CreationMeta::NO_GRAD_MODE);
      ASSERT_EQ(view_out.requires_grad(), requires_grad);
      ASSERT_TRUE(view_out.is_leaf());
    }

    tmp = view_out * view_out;
    ASSERT_EQ(tmp.requires_grad(), requires_grad);
    if (requires_grad) {
      tmp.backward(torch::ones_like(tmp));
      // TODO: this behavior is a side effect of issue #11390.
      ASSERT_FALSE(view_out.grad().defined());
    }

    if (requires_grad) {
      ASSERT_THROWS_WITH(view_out.mul_(2),  // go through kernels: VariableType, ADInplaceOrView, CPU
        "a leaf Variable that requires grad is being used in an in-place operation")
    } else {
        view_out.mul_(2);
    }

    tmp = view_out.view({2, 3});
    ASSERT_EQ(tmp.requires_grad(), requires_grad);
    // TODO: update when above error is fixed
    // assert_tensor_creation_meta(tmp, torch::autograd::CreationMeta::NO_GRAD_MODE);
  }
}
