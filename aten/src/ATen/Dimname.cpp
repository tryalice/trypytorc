#ifdef NAMEDTENSOR_ENABLED
#include <ATen/Dimname.h>
#include <c10/util/Exception.h>

namespace at {

bool is_valid_identifier(const std::string& name) {
  std::locale loc;
  if (name.length() == 0) {
    return false;
  }
  for (auto it = name.begin(); it != name.end(); ++it) {
    if (std::isalpha(*it, loc) || *it == '_') {
      continue;
    }
    return false;
  }
  return true;
}

static void check_valid_identifier(const std::string& name) {
  TORCH_CHECK(
      is_valid_identifier(name),
      "A valid identifier must contain alphabetical characters and/or underscore, got: '",
      name, "'.");
}

Dimname Dimname::fromSymbol(Symbol name) {
  TORCH_INTERNAL_ASSERT(name.is_dimname());
  if (name == kWildcard) {
    return Dimname::wildcard();
  }
  const std::string delimiter = ".";
  const std::string str(name.toUnqualString());
  auto it = str.find(delimiter);

  // Check for normal name
  if (it == std::string::npos) {
    check_valid_identifier(str);
    return Dimname(name);
  }

  // Check for tagged name
  auto second_dot = str.find(delimiter, it + 1);
  TORCH_CHECK(
      second_dot == std::string::npos,
      "Invalid name '", str, "': A tagged name can only contain one '.'");
  auto untagged_name = str.substr(0, it);
  auto tag = str.substr(it + 1);
  check_valid_identifier(untagged_name); 
  check_valid_identifier(tag);
  return Dimname(NameType::TAGGED, name, Symbol::dimname(untagged_name));
}

Dimname Dimname::wildcard() {
  static Dimname result(NameType::WILDCARD, kWildcard, kWildcard);
  return result;
}

optional<Dimname> unify(Dimname dimname, Dimname other) {
  if (other.type() == NameType::WILDCARD) {
    return dimname;
  }
  if (dimname.type() == NameType::WILDCARD) {
    return other;
  }
  if (dimname.name() == other.name()) {
    return dimname;
  }
  if (dimname.untagged_name() == other.untagged_name()) {
    return Dimname::fromSymbol(dimname.untagged_name());
  }
  return c10::nullopt;
}

bool match(Dimname dimname, Dimname other) {
  return unify(dimname, other).has_value();
}

} // namespace at
#endif
