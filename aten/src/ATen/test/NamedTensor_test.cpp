#ifdef NAMEDTENSOR_ENABLED
#include <gtest/gtest.h>

#include <ATen/ATen.h>
#include <ATen/NamedTensor.h>
#include <c10/util/Exception.h>
#include <torch/csrc/utils/memory.h>

using at::Dimname;
using at::NamedTensorMeta;
using at::Symbol;
using torch::make_unique;

TEST(NamedTensorTest, defaultMetadata) {
  int num_names = 4;
  const auto meta = NamedTensorMeta(num_names);
  for (const auto name : meta.names) {
    ASSERT_EQ(name.type(), at::NameType::WILDCARD);
  }
}

TEST(NamedTensorTest, isNamed) {
  auto tensor = at::zeros({3, 2, 5, 7});
  ASSERT_FALSE(tensor.is_named());

  tensor = at::zeros({3, 2, 5, 7});
  tensor.unsafeGetTensorImpl()->set_named_tensor_meta(
      make_unique<NamedTensorMeta>(tensor.dim()));
  ASSERT_FALSE(tensor.is_named());

  tensor = at::zeros({3, 2, 5, 7});
  auto N = Dimname::fromSymbol(Symbol::dimname("N"));
  auto C = Dimname::fromSymbol(Symbol::dimname("C"));
  auto H = Dimname::fromSymbol(Symbol::dimname("H"));
  auto W = Dimname::fromSymbol(Symbol::dimname("W"));
  std::vector<Dimname> names = { N, C, H, W };
  tensor.unsafeGetTensorImpl()->set_named_tensor_meta(
      make_unique<NamedTensorMeta>(names));
  ASSERT_TRUE(tensor.is_named());
}

TEST(NamedTensorTest, attachMetadata) {
  auto tensor = at::zeros({3, 2, 5, 7});
  auto N = Dimname::fromSymbol(Symbol::dimname("N"));
  auto C = Dimname::fromSymbol(Symbol::dimname("C"));
  auto H = Dimname::fromSymbol(Symbol::dimname("H"));
  auto W = Dimname::fromSymbol(Symbol::dimname("W"));
  std::vector<Dimname> names = { N, C, H, W };

  tensor.unsafeGetTensorImpl()->set_named_tensor_meta(
      make_unique<NamedTensorMeta>(names));
  
  const auto retrieved_meta = tensor.get_named_tensor_meta();
  for (int i = 0; i < tensor.dim(); ++i) {
    const auto& retrieved_name = retrieved_meta->names[i];
    const auto& expected_name = names[i];

    ASSERT_EQ(retrieved_name.type(), expected_name.type());
    ASSERT_EQ(retrieved_name.name(), expected_name.name());
  }

  // Test dropping metadata
  tensor.unsafeGetTensorImpl()->set_named_tensor_meta(nullptr);
  ASSERT_FALSE(tensor.is_named());
}
#endif
