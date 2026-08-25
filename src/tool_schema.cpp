// SPDX-License-Identifier: Apache-2.0

#include "cogito/tool_schema.hpp"

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>

namespace cogito {
namespace {

constexpr std::string_view kDraft7Uri = "http://json-schema.org/draft-07/schema#";
constexpr std::string_view kPatternBudgetDiagnostic = "pattern_budget_exhausted";
constexpr std::size_t kMaxPatternNestingDepth = kMaxPatternBytes;
constexpr std::size_t kMaxPatternAstNodes = kMaxPatternBytes * kMaxQuantifierBound;
constexpr std::size_t kMaxCompiledNfaStates =
    kMaxPatternBytes * kMaxQuantifierBound * 8U;

Error CompileError() {
  return Error{Errc::SchemaCompileFailed, {}, "schema compile failed"};
}

Error SchemaError() {
  return Error{Errc::SchemaViolation, reason::kSchemaViolation, "schema violation"};
}

Error PatternBudgetError() {
  return Error{Errc::PatternBudgetExhausted, reason::kPatternTimeout,
               "pattern step budget exhausted"};
}

std::string BoundedDiagnostic(std::string value) {
  if (value.size() <= kMaxDiagnosticBytes) {
    return value;
  }
  std::size_t retained = kMaxDiagnosticBytes - 3U;
  while (retained > 0U &&
         (static_cast<unsigned char>(value[retained]) & 0xC0U) == 0x80U) {
    --retained;
  }
  value.resize(retained);
  value += "...";
  return value;
}

bool DecodeUtf8(std::string_view text, std::vector<std::uint8_t>* scalar_lengths) {
  if (scalar_lengths != nullptr) {
    scalar_lengths->assign(text.size(), 0U);
  }

  std::size_t i = 0U;
  while (i < text.size()) {
    const auto byte0 = static_cast<std::uint8_t>(text[i]);
    std::size_t length = 0U;
    if (byte0 <= 0x7FU) {
      length = 1U;
    } else if (byte0 >= 0xC2U && byte0 <= 0xDFU) {
      length = 2U;
    } else if (byte0 >= 0xE0U && byte0 <= 0xEFU) {
      length = 3U;
    } else if (byte0 >= 0xF0U && byte0 <= 0xF4U) {
      length = 4U;
    } else {
      return false;
    }

    if (length > text.size() - i) {
      return false;
    }
    for (std::size_t offset = 1U; offset < length; ++offset) {
      const auto continuation = static_cast<std::uint8_t>(text[i + offset]);
      if ((continuation & 0xC0U) != 0x80U) {
        return false;
      }
    }

    if (length == 3U) {
      const auto byte1 = static_cast<std::uint8_t>(text[i + 1U]);
      if ((byte0 == 0xE0U && byte1 < 0xA0U) ||
          (byte0 == 0xEDU && byte1 >= 0xA0U)) {
        return false;
      }
    } else if (length == 4U) {
      const auto byte1 = static_cast<std::uint8_t>(text[i + 1U]);
      if ((byte0 == 0xF0U && byte1 < 0x90U) ||
          (byte0 == 0xF4U && byte1 >= 0x90U)) {
        return false;
      }
    }

    if (scalar_lengths != nullptr) {
      (*scalar_lengths)[i] = static_cast<std::uint8_t>(length);
    }
    i += length;
  }
  return true;
}

bool IsEscaped(std::string_view pattern, std::size_t index) {
  std::size_t slashes = 0U;
  while (index > slashes && pattern[index - slashes - 1U] == '\\') {
    ++slashes;
  }
  return (slashes % 2U) != 0U;
}

enum class NodeKind : std::uint8_t {
  Empty,
  Literal,
  ByteClass,
  Dot,
  Concat,
  Alternate,
  Group,
  Repeat
};

struct PatternNode {
  NodeKind kind = NodeKind::Empty;
  std::vector<std::uint8_t> literal;
  std::bitset<256> bytes;
  std::vector<std::unique_ptr<PatternNode>> children;
  std::size_t repeat_min = 0U;
  std::size_t repeat_max = 0U;
  bool repeat_unbounded = false;
};

bool ContainsRepeat(const PatternNode& node) {
  if (node.kind == NodeKind::Repeat) {
    return true;
  }
  for (const auto& child : node.children) {
    if (ContainsRepeat(*child)) {
      return true;
    }
  }
  return false;
}

bool IsSingleRepeatableAtom(const PatternNode& node) {
  return node.kind == NodeKind::Literal || node.kind == NodeKind::ByteClass ||
         node.kind == NodeKind::Dot;
}

class PatternSyntaxError final {};

class PatternParser {
 public:
  PatternParser(std::string_view pattern, bool enforce_quantifier_limit)
      : pattern_(pattern), enforce_quantifier_limit_(enforce_quantifier_limit) {}

  std::unique_ptr<PatternNode> Parse() {
    auto expression = ParseExpression();
    if (position_ != pattern_.size()) {
      throw PatternSyntaxError{};
    }
    return expression;
  }

 private:
  bool AtEnd() const noexcept { return position_ >= pattern_.size(); }

  char Peek() const noexcept { return AtEnd() ? '\0' : pattern_[position_]; }

  char Take() {
    if (AtEnd()) {
      throw PatternSyntaxError{};
    }
    return pattern_[position_++];
  }

  std::unique_ptr<PatternNode> MakeNode(NodeKind kind) {
    if (node_count_ >= kMaxPatternAstNodes) {
      throw PatternSyntaxError{};
    }
    ++node_count_;
    auto node = std::make_unique<PatternNode>();
    node->kind = kind;
    return node;
  }

  static bool IsQuantifierStart(char value) noexcept {
    return value == '*' || value == '+' || value == '?' || value == '{';
  }

  std::unique_ptr<PatternNode> ParseExpression() {
    std::vector<std::unique_ptr<PatternNode>> branches;
    branches.push_back(ParseConcat());
    while (!AtEnd() && Peek() == '|') {
      ++position_;
      branches.push_back(ParseConcat());
    }
    if (branches.size() == 1U) {
      return std::move(branches.front());
    }
    auto node = MakeNode(NodeKind::Alternate);
    node->children = std::move(branches);
    return node;
  }

  std::unique_ptr<PatternNode> ParseConcat() {
    std::vector<std::unique_ptr<PatternNode>> sequence;
    while (!AtEnd() && Peek() != ')' && Peek() != '|') {
      sequence.push_back(ParseRepetition());
    }
    if (sequence.empty()) {
      return MakeNode(NodeKind::Empty);
    }
    if (sequence.size() == 1U) {
      return std::move(sequence.front());
    }
    auto node = MakeNode(NodeKind::Concat);
    node->children = std::move(sequence);
    return node;
  }

  std::unique_ptr<PatternNode> ParseRepetition() {
    auto atom = ParseAtom();
    if (AtEnd() || !IsQuantifierStart(Peek())) {
      return atom;
    }

    const char quantifier = Take();
    if (ContainsRepeat(*atom)) {
      throw PatternSyntaxError{};
    }
    if ((quantifier == '*' || quantifier == '+') && !IsSingleRepeatableAtom(*atom)) {
      throw PatternSyntaxError{};
    }

    auto repeated = MakeNode(NodeKind::Repeat);
    repeated->children.push_back(std::move(atom));
    if (quantifier == '*') {
      repeated->repeat_min = 0U;
      repeated->repeat_unbounded = true;
    } else if (quantifier == '+') {
      repeated->repeat_min = 1U;
      repeated->repeat_unbounded = true;
    } else if (quantifier == '?') {
      repeated->repeat_min = 0U;
      repeated->repeat_max = 1U;
    } else {
      repeated->repeat_min = ParseUnsigned();
      if (AtEnd() || Take() != ',') {
        throw PatternSyntaxError{};
      }
      repeated->repeat_max = ParseUnsigned();
      if (Take() != '}' || repeated->repeat_min > repeated->repeat_max) {
        throw PatternSyntaxError{};
      }
      if (enforce_quantifier_limit_ &&
          repeated->repeat_max > kMaxQuantifierBound) {
        throw PatternSyntaxError{};
      }
    }

    if (!AtEnd() && IsQuantifierStart(Peek())) {
      throw PatternSyntaxError{};
    }
    return repeated;
  }

  std::size_t ParseUnsigned() {
    if (AtEnd() || Peek() < '0' || Peek() > '9') {
      throw PatternSyntaxError{};
    }
    std::size_t value = 0U;
    do {
      const std::size_t digit = static_cast<std::size_t>(Take() - '0');
      if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10U) {
        throw PatternSyntaxError{};
      }
      value = value * 10U + digit;
    } while (!AtEnd() && Peek() >= '0' && Peek() <= '9');
    return value;
  }

  std::unique_ptr<PatternNode> ParseAtom() {
    if (AtEnd()) {
      throw PatternSyntaxError{};
    }
    const char value = Take();
    if (value == '(') {
      if (!AtEnd() && Peek() == '?') {
        throw PatternSyntaxError{};
      }
      if (group_depth_ >= kMaxPatternNestingDepth) {
        throw PatternSyntaxError{};
      }
      ++group_depth_;
      auto child = ParseExpression();
      --group_depth_;
      if (AtEnd() || Take() != ')') {
        throw PatternSyntaxError{};
      }
      auto group = MakeNode(NodeKind::Group);
      group->children.push_back(std::move(child));
      return group;
    }
    if (value == '.') {
      return MakeNode(NodeKind::Dot);
    }
    if (value == '[') {
      return ParseClass();
    }
    if (value == '\\') {
      return ParseEscape(false);
    }
    if (value == ')' || value == '|' || value == '*' || value == '+' || value == '?' ||
        value == '{' || value == '}' || value == '^' || value == '$' || value == ']') {
      throw PatternSyntaxError{};
    }

    auto literal = MakeNode(NodeKind::Literal);
    const auto first = static_cast<std::uint8_t>(value);
    std::size_t scalar_length = 1U;
    if (first >= 0xC2U && first <= 0xDFU) {
      scalar_length = 2U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      scalar_length = 3U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      scalar_length = 4U;
    }
    literal->literal.push_back(first);
    for (std::size_t i = 1U; i < scalar_length; ++i) {
      if (AtEnd()) {
        throw PatternSyntaxError{};
      }
      literal->literal.push_back(static_cast<std::uint8_t>(Take()));
    }
    return literal;
  }

  std::unique_ptr<PatternNode> ParseEscape(bool in_class) {
    if (AtEnd()) {
      throw PatternSyntaxError{};
    }
    const char escaped = Take();
    if (escaped >= '0' && escaped <= '9') {
      throw PatternSyntaxError{};
    }

    auto byte_class = MakeNode(NodeKind::ByteClass);
    if (escaped == 'd') {
      for (unsigned value = static_cast<unsigned>('0'); value <= static_cast<unsigned>('9');
           ++value) {
        byte_class->bytes.set(value);
      }
      return byte_class;
    }
    if (escaped == 'w') {
      for (unsigned value = static_cast<unsigned>('0'); value <= static_cast<unsigned>('9');
           ++value) {
        byte_class->bytes.set(value);
      }
      for (unsigned value = static_cast<unsigned>('A'); value <= static_cast<unsigned>('Z');
           ++value) {
        byte_class->bytes.set(value);
      }
      for (unsigned value = static_cast<unsigned>('a'); value <= static_cast<unsigned>('z');
           ++value) {
        byte_class->bytes.set(value);
      }
      byte_class->bytes.set(static_cast<unsigned>('_'));
      return byte_class;
    }
    if (escaped == 's') {
      for (const unsigned char value : {' ', '\t', '\r', '\n', '\f', '\v'}) {
        byte_class->bytes.set(value);
      }
      return byte_class;
    }

    const std::string_view allowed = in_class ? std::string_view{"]-^\\"}
                                               : std::string_view{".\\^$[](){}|*+?-"};
    if (allowed.find(escaped) == std::string_view::npos) {
      throw PatternSyntaxError{};
    }
    auto literal = MakeNode(NodeKind::Literal);
    literal->literal.push_back(static_cast<std::uint8_t>(escaped));
    return literal;
  }

  struct ClassItem {
    std::bitset<256> bytes;
    bool single = false;
    std::uint8_t value = 0U;
  };

  ClassItem ParseClassItem() {
    if (AtEnd() || Peek() == ']') {
      throw PatternSyntaxError{};
    }
    const char current = Take();
    if (current == '\\') {
      auto escaped = ParseEscape(true);
      ClassItem item;
      if (escaped->kind == NodeKind::Literal) {
        item.bytes.set(escaped->literal.front());
        item.single = true;
        item.value = escaped->literal.front();
      } else {
        item.bytes = escaped->bytes;
      }
      return item;
    }
    const auto value = static_cast<std::uint8_t>(current);
    ClassItem item;
    item.bytes.set(value);
    item.single = true;
    item.value = value;
    return item;
  }

  std::unique_ptr<PatternNode> ParseClass() {
    bool negated = false;
    if (!AtEnd() && Peek() == '^') {
      negated = true;
      ++position_;
    }
    if (AtEnd() || Peek() == ']') {
      throw PatternSyntaxError{};
    }

    std::bitset<256> values;
    bool found_item = false;
    while (!AtEnd() && Peek() != ']') {
      ClassItem first = ParseClassItem();
      found_item = true;
      if (!AtEnd() && Peek() == '-' && position_ + 1U < pattern_.size() &&
          pattern_[position_ + 1U] != ']') {
        ++position_;
        ClassItem last = ParseClassItem();
        if (!first.single || !last.single || first.value > last.value) {
          throw PatternSyntaxError{};
        }
        for (unsigned value = first.value; value <= last.value; ++value) {
          values.set(value);
        }
      } else {
        values |= first.bytes;
      }
    }
    if (!found_item || AtEnd() || Take() != ']') {
      throw PatternSyntaxError{};
    }
    if (negated) {
      values.flip();
    }
    auto node = MakeNode(NodeKind::ByteClass);
    node->bytes = values;
    return node;
  }

  std::string_view pattern_;
  std::size_t position_ = 0U;
  std::size_t group_depth_ = 0U;
  std::size_t node_count_ = 0U;
  bool enforce_quantifier_limit_ = true;
};

std::size_t SaturatingMultiply(std::size_t lhs, std::size_t rhs, std::size_t cap) {
  if (lhs == 0U || rhs == 0U) {
    return 0U;
  }
  if (lhs > cap / rhs) {
    return cap + 1U;
  }
  return lhs * rhs;
}

std::size_t SaturatingAdd(std::size_t lhs, std::size_t rhs, std::size_t cap) {
  if (lhs > cap || rhs > cap || lhs > cap - rhs) {
    return cap + 1U;
  }
  return lhs + rhs;
}

std::size_t SaturatingPower(std::size_t base, std::size_t exponent, std::size_t cap) {
  std::size_t result = 1U;
  for (std::size_t i = 0U; i < exponent; ++i) {
    result = SaturatingMultiply(result, base, cap);
    if (result > cap) {
      return result;
    }
  }
  return result;
}

std::size_t AlternationProduct(const PatternNode& node, std::size_t cap) {
  std::size_t product = 1U;
  if (node.kind == NodeKind::Alternate) {
    product = node.children.size();
  }
  for (const auto& child : node.children) {
    std::size_t child_product = AlternationProduct(*child, cap);
    if (node.kind == NodeKind::Repeat && !node.repeat_unbounded) {
      child_product = SaturatingPower(child_product, node.repeat_max, cap);
    }
    product = SaturatingMultiply(product, child_product, cap);
    if (product > cap) {
      return product;
    }
  }
  return product;
}

std::size_t EstimateNfaStates(const PatternNode& node, std::size_t cap) {
  switch (node.kind) {
    case NodeKind::Empty:
      return 1U;
    case NodeKind::Literal:
      return node.literal.empty() ? 1U : node.literal.size();
    case NodeKind::ByteClass:
    case NodeKind::Dot:
      return 1U;
    case NodeKind::Concat: {
      std::size_t total = 0U;
      for (const auto& child : node.children) {
        total = SaturatingAdd(total, EstimateNfaStates(*child, cap), cap);
        if (total > cap) {
          return total;
        }
      }
      return total == 0U ? 1U : total;
    }
    case NodeKind::Alternate: {
      std::size_t total = node.children.empty() ? 1U : node.children.size() - 1U;
      for (const auto& child : node.children) {
        total = SaturatingAdd(total, EstimateNfaStates(*child, cap), cap);
        if (total > cap) {
          return total;
        }
      }
      return total;
    }
    case NodeKind::Group:
      return SaturatingAdd(EstimateNfaStates(*node.children.front(), cap), 2U, cap);
    case NodeKind::Repeat: {
      const std::size_t child_states = EstimateNfaStates(*node.children.front(), cap);
      if (node.repeat_unbounded) {
        return SaturatingAdd(child_states, 1U, cap);
      }
      if (node.repeat_max == 0U) {
        return 1U;
      }
      std::size_t total = SaturatingMultiply(child_states, node.repeat_max, cap);
      total = SaturatingAdd(total, node.repeat_max - node.repeat_min, cap);
      return total;
    }
  }
  return cap + 1U;
}

enum class StateKind : std::uint8_t { Consume, Epsilon, Split, Accept };
enum class PredicateKind : std::uint8_t { Bytes, UnicodeScalar };

struct NfaState {
  StateKind kind = StateKind::Accept;
  PredicateKind predicate = PredicateKind::Bytes;
  std::bitset<256> bytes;
  int out1 = -1;
  int out2 = -1;
};

struct PatchRef {
  int state = -1;
  bool second = false;
};

struct Fragment {
  int start = -1;
  std::vector<PatchRef> outs;
};

struct PatternProgram {
  std::vector<NfaState> states;
  int start = -1;
  int accept = -1;
};

class NfaCompiler {
 public:
  PatternProgram Compile(const PatternNode& root) {
    Fragment fragment = CompileNode(root);
    NfaState accept_state;
    accept_state.kind = StateKind::Accept;
    const int accept = AddState(std::move(accept_state));
    Patch(fragment.outs, accept);
    NfaState start_state;
    start_state.kind = StateKind::Epsilon;
    start_state.out1 = fragment.start;
    const int start = AddState(std::move(start_state));
    PatternProgram program;
    program.states = std::move(states_);
    program.start = start;
    program.accept = accept;
    return program;
  }

 private:
  int AddState(NfaState state) {
    if (states_.size() >= kMaxCompiledNfaStates ||
        states_.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      throw PatternSyntaxError{};
    }
    states_.push_back(std::move(state));
    return static_cast<int>(states_.size() - 1U);
  }

  void Patch(const std::vector<PatchRef>& refs, int target) {
    for (const PatchRef& ref : refs) {
      if (ref.second) {
        states_[static_cast<std::size_t>(ref.state)].out2 = target;
      } else {
        states_[static_cast<std::size_t>(ref.state)].out1 = target;
      }
    }
  }

  static void AppendOuts(std::vector<PatchRef>& destination,
                         const std::vector<PatchRef>& source) {
    destination.insert(destination.end(), source.begin(), source.end());
  }

  Fragment EmptyFragment() {
    NfaState state;
    state.kind = StateKind::Epsilon;
    const int index = AddState(std::move(state));
    return Fragment{index, {{index, false}}};
  }

  Fragment ConsumeBytes(const std::bitset<256>& bytes) {
    NfaState state;
    state.kind = StateKind::Consume;
    state.predicate = PredicateKind::Bytes;
    state.bytes = bytes;
    const int index = AddState(std::move(state));
    return Fragment{index, {{index, false}}};
  }

  Fragment ConsumeDot() {
    NfaState state;
    state.kind = StateKind::Consume;
    state.predicate = PredicateKind::UnicodeScalar;
    const int index = AddState(std::move(state));
    return Fragment{index, {{index, false}}};
  }

  Fragment Concat(Fragment lhs, Fragment rhs) {
    if (lhs.start < 0) {
      return rhs;
    }
    if (rhs.start < 0) {
      return lhs;
    }
    Patch(lhs.outs, rhs.start);
    lhs.outs = std::move(rhs.outs);
    return lhs;
  }

  Fragment CompileNode(const PatternNode& node) {
    switch (node.kind) {
      case NodeKind::Empty:
        return EmptyFragment();
      case NodeKind::Literal: {
        Fragment result;
        for (const std::uint8_t value : node.literal) {
          std::bitset<256> bytes;
          bytes.set(value);
          result = Concat(std::move(result), ConsumeBytes(bytes));
        }
        return result.start < 0 ? EmptyFragment() : result;
      }
      case NodeKind::ByteClass:
        return ConsumeBytes(node.bytes);
      case NodeKind::Dot:
        return ConsumeDot();
      case NodeKind::Concat: {
        Fragment result;
        for (const auto& child : node.children) {
          result = Concat(std::move(result), CompileNode(*child));
        }
        return result.start < 0 ? EmptyFragment() : result;
      }
      case NodeKind::Alternate: {
        std::vector<Fragment> branches;
        branches.reserve(node.children.size());
        for (const auto& child : node.children) {
          branches.push_back(CompileNode(*child));
        }
        Fragment result = std::move(branches.back());
        for (std::size_t i = branches.size() - 1U; i > 0U; --i) {
          Fragment left = std::move(branches[i - 1U]);
          NfaState split;
          split.kind = StateKind::Split;
          split.out1 = left.start;
          split.out2 = result.start;
          const int index = AddState(std::move(split));
          AppendOuts(left.outs, result.outs);
          result = Fragment{index, std::move(left.outs)};
        }
        return result;
      }
      case NodeKind::Group: {
        Fragment child = CompileNode(*node.children.front());
        NfaState entry;
        entry.kind = StateKind::Epsilon;
        entry.out1 = child.start;
        const int entry_index = AddState(std::move(entry));
        NfaState exit;
        exit.kind = StateKind::Epsilon;
        const int exit_index = AddState(std::move(exit));
        Patch(child.outs, exit_index);
        return Fragment{entry_index, {{exit_index, false}}};
      }
      case NodeKind::Repeat:
        return CompileRepeat(node);
    }
    throw PatternSyntaxError{};
  }

  Fragment CompileRepeat(const PatternNode& node) {
    const PatternNode& child_node = *node.children.front();
    if (node.repeat_unbounded) {
      Fragment child = CompileNode(child_node);
      NfaState split;
      split.kind = StateKind::Split;
      split.out1 = child.start;
      const int split_index = AddState(std::move(split));
      Patch(child.outs, split_index);
      if (node.repeat_min == 0U) {
        return Fragment{split_index, {{split_index, true}}};
      }
      return Fragment{child.start, {{split_index, true}}};
    }

    Fragment result;
    for (std::size_t i = 0U; i < node.repeat_min; ++i) {
      result = Concat(std::move(result), CompileNode(child_node));
    }
    for (std::size_t i = node.repeat_min; i < node.repeat_max; ++i) {
      Fragment optional = CompileNode(child_node);
      NfaState split;
      split.kind = StateKind::Split;
      split.out1 = optional.start;
      const int split_index = AddState(std::move(split));
      Fragment choice{split_index, std::move(optional.outs)};
      choice.outs.push_back(PatchRef{split_index, true});
      result = Concat(std::move(result), std::move(choice));
    }
    return result.start < 0 ? EmptyFragment() : result;
  }

  std::vector<NfaState> states_;
};

Result<PatternProgram> CompilePattern(std::string_view raw_pattern, bool bypass_static_limits) {
  try {
    if (!DecodeUtf8(raw_pattern, nullptr)) {
      return CompileError();
    }
    if (!bypass_static_limits && raw_pattern.size() > kMaxPatternBytes) {
      return CompileError();
    }

    bool has_start_anchor = !raw_pattern.empty() && raw_pattern.front() == '^';
    bool has_end_anchor = !raw_pattern.empty() && raw_pattern.back() == '$' &&
                          !IsEscaped(raw_pattern, raw_pattern.size() - 1U);
    if (!bypass_static_limits && (!has_start_anchor || !has_end_anchor)) {
      return CompileError();
    }
    if (has_start_anchor) {
      raw_pattern.remove_prefix(1U);
    }
    if (has_end_anchor && !raw_pattern.empty()) {
      raw_pattern.remove_suffix(1U);
    }

    PatternParser parser(raw_pattern, !bypass_static_limits);
    auto tree = parser.Parse();
    if (!bypass_static_limits &&
        AlternationProduct(*tree, kMaxAlternationProduct) > kMaxAlternationProduct) {
      return CompileError();
    }
    const std::size_t estimated_states = EstimateNfaStates(*tree, kMaxCompiledNfaStates);
    if (estimated_states > kMaxCompiledNfaStates - 2U) {
      return CompileError();
    }
    NfaCompiler compiler;
    return Result<PatternProgram>{compiler.Compile(*tree)};
  } catch (const PatternSyntaxError&) {
    return CompileError();
  } catch (...) {
    return CompileError();
  }
}

class StepCounter {
 public:
  explicit StepCounter(std::uint64_t limit) : limit_(limit) {}

  bool Charge(std::uint64_t amount = 1U) noexcept {
    if (amount > limit_ - used_) {
      return false;
    }
    used_ += amount;
    return true;
  }

 private:
  std::uint64_t used_ = 0U;
  std::uint64_t limit_ = 0U;
};

enum class ClosureResult : std::uint8_t { Ok, Accepted, Exhausted };

ClosureResult EpsilonClosure(const PatternProgram& program,
                             const std::vector<int>& seeds,
                             std::vector<int>& active,
                             StepCounter& counter,
                             std::vector<std::uint32_t>& visited,
                             std::uint32_t generation,
                             bool accept_immediately) {
  std::deque<int> pending;
  bool accepted = false;
  for (const int seed : seeds) {
    if (seed >= 0 && visited[static_cast<std::size_t>(seed)] != generation) {
      visited[static_cast<std::size_t>(seed)] = generation;
      pending.push_back(seed);
    }
  }

  while (!pending.empty()) {
    const int index = pending.front();
    pending.pop_front();
    const NfaState& state = program.states[static_cast<std::size_t>(index)];
    if (state.kind == StateKind::Accept) {
      active.push_back(index);
      accepted = true;
      continue;
    }
    if (state.kind == StateKind::Consume) {
      active.push_back(index);
      continue;
    }

    const auto visit_edge = [&](int target) -> bool {
      if (!counter.Charge()) {
        return false;
      }
      if (target >= 0 && visited[static_cast<std::size_t>(target)] != generation) {
        visited[static_cast<std::size_t>(target)] = generation;
        pending.push_back(target);
      }
      return true;
    };

    if (!visit_edge(state.out1)) {
      return ClosureResult::Exhausted;
    }
    if (state.kind == StateKind::Split && !visit_edge(state.out2)) {
      return ClosureResult::Exhausted;
    }
  }
  return accept_immediately && accepted ? ClosureResult::Accepted : ClosureResult::Ok;
}

Result<bool> MatchProgram(const PatternProgram& program,
                          std::string_view input,
                          std::uint64_t step_budget) {
  if (input.size() > kMaxMatchStringBytes) {
    return SchemaError();
  }
  std::vector<std::uint8_t> scalar_lengths;
  if (!DecodeUtf8(input, &scalar_lengths)) {
    return SchemaError();
  }
  if (step_budget == 0U) {
    return PatternBudgetError();
  }

  StepCounter counter(step_budget);
  std::vector<std::vector<int>> positions(input.size() + 1U);
  std::vector<std::uint32_t> visited(program.states.size(), 0U);
  std::uint32_t generation = 0U;
  positions.front().push_back(program.start);
  for (std::size_t position = 0U; position <= input.size(); ++position) {
    if (positions[position].empty()) {
      continue;
    }
    ++generation;
    if (generation == 0U) {
      std::fill(visited.begin(), visited.end(), 0U);
      generation = 1U;
    }
    std::vector<int> active;
    const ClosureResult closure = EpsilonClosure(program, positions[position], active, counter,
                                                 visited, generation,
                                                 position == input.size());
    if (closure == ClosureResult::Accepted) {
      return Result<bool>{true};
    }
    if (closure == ClosureResult::Exhausted) {
      return PatternBudgetError();
    }
    if (position == input.size()) {
      continue;
    }

    const auto byte = static_cast<std::uint8_t>(input[position]);
    for (const int index : active) {
      const NfaState& state = program.states[static_cast<std::size_t>(index)];
      if (state.kind != StateKind::Consume) {
        continue;
      }

      if (state.predicate == PredicateKind::Bytes) {
        if (!counter.Charge()) {
          return PatternBudgetError();
        }
        if (state.bytes.test(byte)) {
          positions[position + 1U].push_back(state.out1);
        }
        continue;
      }

      const std::size_t scalar_length = scalar_lengths[position];
      const bool matches = scalar_length != 0U && byte != 0x0AU && byte != 0x0DU;
      const std::uint64_t charge = matches ? static_cast<std::uint64_t>(scalar_length) : 1U;
      if (!counter.Charge(charge)) {
        return PatternBudgetError();
      }
      if (matches) {
        positions[position + scalar_length].push_back(state.out1);
      }
    }
  }
  return Result<bool>{false};
}

enum class PathStepKind : std::uint8_t { Property, EachItem };

struct PathStep {
  PathStepKind kind = PathStepKind::Property;
  std::string property;
};

struct PatternRule {
  std::vector<PathStep> path;
  PatternProgram program;
};

std::string EscapePointerToken(std::string_view token) {
  std::string escaped;
  for (const char value : token) {
    if (value == '~') {
      escaped += "~0";
    } else if (value == '/') {
      escaped += "~1";
    } else {
      escaped.push_back(value);
    }
  }
  return escaped;
}

bool IsInteger(const ccj::Json& value) {
  return value.is_number_integer() || value.is_number_unsigned();
}

bool ReadNonnegativeSize(const ccj::Json& value, std::uint64_t& output) {
  if (value.is_number_unsigned()) {
    output = value.get<std::uint64_t>();
    return true;
  }
  if (!value.is_number_integer()) {
    return false;
  }
  const auto signed_value = value.get<std::int64_t>();
  if (signed_value < 0) {
    return false;
  }
  output = static_cast<std::uint64_t>(signed_value);
  return true;
}

class SchemaAnalyzer {
 public:
  Result<std::vector<PatternRule>> Analyze(const ccj::Json& source,
                                           ccj::Json& stripped,
                                           SchemaAudit& audit) {
    try {
      std::vector<PathStep> path;
      if (!Walk(source, stripped, true, path)) {
        return CompileError();
      }
      audit.tier_v_keywords.assign(tier_v_keywords_.begin(), tier_v_keywords_.end());
      if (!tier_v_keywords_.empty()) {
        audit.coverage = GrammarCoverage::Partial;
      } else if (has_tier_g_constraint_) {
        audit.coverage = GrammarCoverage::Full;
      } else {
        audit.coverage = GrammarCoverage::None;
      }
      return Result<std::vector<PatternRule>>{std::move(patterns_)};
    } catch (...) {
      return CompileError();
    }
  }

 private:
  bool Walk(const ccj::Json& schema,
            ccj::Json& stripped,
            bool is_root,
            std::vector<PathStep>& path) {
    if (!schema.is_object() || !stripped.is_object()) {
      return false;
    }

    static const std::set<std::string> forbidden{
        "$ref",          "definitions",        "$id",       "patternProperties",
        "dependencies",  "allOf",             "anyOf",     "oneOf",
        "not",           "if",                "then",      "else",
        "propertyNames", "contains",           "additionalItems",
        "default"};

    std::string node_type;
    const auto type_it = schema.find("type");
    if (type_it != schema.end()) {
      if (!type_it->is_string()) {
        return false;
      }
      node_type = type_it->get<std::string>();
    }

    for (auto it = schema.begin(); it != schema.end(); ++it) {
      const std::string& keyword = it.key();
      const ccj::Json& value = it.value();
      if (forbidden.find(keyword) != forbidden.end()) {
        return false;
      }
      if (keyword == "$schema") {
        if (!is_root || !value.is_string() ||
            value.get<std::string>() != std::string(kDraft7Uri)) {
          return false;
        }
      } else if (keyword == "title" || keyword == "description") {
        if (!is_root || !value.is_string()) {
          return false;
        }
      } else if (keyword == "type") {
        static const std::set<std::string> allowed_types{
            "object", "array", "string", "integer", "boolean", "number"};
        if (allowed_types.find(node_type) == allowed_types.end()) {
          return false;
        }
        if (node_type == "number") {
          tier_v_keywords_.insert("type:number");
        }
      } else if (keyword == "properties") {
        if (!value.is_object()) {
          return false;
        }
        for (auto property = value.begin(); property != value.end(); ++property) {
          path.push_back(PathStep{PathStepKind::Property, property.key()});
          if (!Walk(property.value(), stripped["properties"][property.key()], false, path)) {
            return false;
          }
          path.pop_back();
        }
      } else if (keyword == "required") {
        if (!value.is_array()) {
          return false;
        }
        std::set<std::string> names;
        for (const auto& name : value) {
          if (!name.is_string() || !names.insert(name.get<std::string>()).second) {
            return false;
          }
        }
      } else if (keyword == "additionalProperties") {
        if (!value.is_boolean() || value.get<bool>()) {
          return false;
        }
      } else if (keyword == "enum") {
        if (!value.is_array() || value.empty()) {
          return false;
        }
        for (const auto& member : value) {
          if (!member.is_string() && !IsInteger(member)) {
            return false;
          }
        }
        has_tier_g_constraint_ = true;
      } else if (keyword == "minLength" || keyword == "maxLength") {
        std::uint64_t ignored = 0U;
        if (!ReadNonnegativeSize(value, ignored)) {
          return false;
        }
        has_tier_g_constraint_ = true;
      } else if (keyword == "minimum" || keyword == "maximum") {
        if (!value.is_number()) {
          return false;
        }
        if (node_type == "number") {
          tier_v_keywords_.insert("number_bounds");
        } else if (node_type == "integer" && IsInteger(value)) {
          has_tier_g_constraint_ = true;
        } else {
          return false;
        }
      } else if (keyword == "exclusiveMinimum" || keyword == "exclusiveMaximum") {
        if (!value.is_number()) {
          return false;
        }
        tier_v_keywords_.insert("exclusive_bounds");
      } else if (keyword == "const") {
        tier_v_keywords_.insert("const");
      } else if (keyword == "minItems" || keyword == "maxItems") {
        std::uint64_t ignored = 0U;
        if (!ReadNonnegativeSize(value, ignored)) {
          return false;
        }
        tier_v_keywords_.insert(keyword == "minItems" ? "min_items" : "max_items");
      } else if (keyword == "items") {
        if (!value.is_object()) {
          return false;
        }
        tier_v_keywords_.insert("items");
        path.push_back(PathStep{PathStepKind::EachItem, {}});
        if (!Walk(value, stripped["items"], false, path)) {
          return false;
        }
        path.pop_back();
      } else if (keyword == "pattern") {
        if (!value.is_string()) {
          return false;
        }
        const auto max_it = schema.find("maxLength");
        std::uint64_t max_length = 0U;
        if (max_it == schema.end() || !ReadNonnegativeSize(*max_it, max_length) ||
            max_length > kMaxMatchStringBytes) {
          return false;
        }
        auto compiled = CompilePattern(value.get_ref<const std::string&>(), false);
        if (!compiled.ok()) {
          return false;
        }
        PatternRule rule;
        rule.path = path;
        rule.program = std::move(compiled).take();
        patterns_.push_back(std::move(rule));
        stripped.erase("pattern");
        has_tier_g_constraint_ = true;
      } else {
        return false;
      }
    }
    return true;
  }

  bool has_tier_g_constraint_ = false;
  std::set<std::string> tier_v_keywords_;
  std::vector<PatternRule> patterns_;
};

class FirstErrorHandler final : public nlohmann::json_schema::error_handler {
 public:
  void error(const nlohmann::json::json_pointer& pointer,
             const nlohmann::json& instance,
             const std::string& message) override {
    static_cast<void>(instance);
    static_cast<void>(message);
    if (!seen_) {
      pointer_ = pointer.to_string();
      seen_ = true;
    }
  }

  bool seen() const noexcept { return seen_; }
  const std::string& pointer() const noexcept { return pointer_; }

 private:
  bool seen_ = false;
  std::string pointer_;
};

Result<bool> EvaluatePatternPath(const PatternRule& rule,
                                 const ccj::Json& value,
                                 std::size_t path_index,
                                 std::string pointer,
                                 std::string& failure_pointer) {
  if (path_index == rule.path.size()) {
    if (!value.is_string()) {
      return Result<bool>{true};
    }
    auto matched = MatchProgram(rule.program, value.get_ref<const std::string&>(),
                                kPatternMatchStepBudget);
    if (!matched.ok()) {
      failure_pointer = pointer;
      return matched.error();
    }
    if (!matched.value()) {
      failure_pointer = pointer;
      return Result<bool>{false};
    }
    return Result<bool>{true};
  }

  const PathStep& step = rule.path[path_index];
  if (step.kind == PathStepKind::Property) {
    if (!value.is_object()) {
      return Result<bool>{true};
    }
    const auto child = value.find(step.property);
    if (child == value.end()) {
      return Result<bool>{true};
    }
    pointer += "/" + EscapePointerToken(step.property);
    return EvaluatePatternPath(rule, *child, path_index + 1U, std::move(pointer),
                               failure_pointer);
  }

  if (!value.is_array()) {
    return Result<bool>{true};
  }
  for (std::size_t i = 0U; i < value.size(); ++i) {
    auto result = EvaluatePatternPath(rule, value[i], path_index + 1U,
                                      pointer + "/" + std::to_string(i), failure_pointer);
    if (!result.ok() || !result.value()) {
      return result;
    }
  }
  return Result<bool>{true};
}

}  // namespace

struct CompiledSchema::Impl {
  Impl()
      : validator(nlohmann::json_schema::schema_loader{},
                  nlohmann::json_schema::format_checker{},
                  nlohmann::json_schema::content_checker{}) {}

  nlohmann::json_schema::json_validator validator;
  SchemaAudit audit;
  std::vector<PatternRule> patterns;
};

const char* ToString(GrammarCoverage coverage) noexcept {
  switch (coverage) {
    case GrammarCoverage::Full:
      return "full";
    case GrammarCoverage::Partial:
      return "partial";
    case GrammarCoverage::None:
      return "none";
  }
  return "none";
}

CompiledSchema::CompiledSchema() : p_(std::make_unique<Impl>()) {}

CompiledSchema::~CompiledSchema() = default;

std::string CompiledSchema::Check(const ccj::Json& doc) const {
  try {
    FirstErrorHandler handler;
    static_cast<void>(p_->validator.validate(doc, handler));
    if (handler.seen()) {
      const std::string pointer = handler.pointer().empty() ? "/" : handler.pointer();
      return BoundedDiagnostic(pointer + ": schema violation");
    }

    for (const PatternRule& rule : p_->patterns) {
      std::string failure_pointer;
      auto matched = EvaluatePatternPath(rule, doc, 0U, {}, failure_pointer);
      if (!matched.ok()) {
        if (matched.error().code == Errc::PatternBudgetExhausted) {
          return std::string(kPatternBudgetDiagnostic);
        }
        const std::string pointer = failure_pointer.empty() ? "/" : failure_pointer;
        return BoundedDiagnostic(pointer + ": pattern input invalid");
      }
      if (!matched.value()) {
        const std::string pointer = failure_pointer.empty() ? "/" : failure_pointer;
        return BoundedDiagnostic(pointer + ": pattern mismatch");
      }
    }
    return {};
  } catch (...) {
    return "schema validation failed";
  }
}

Error CompiledSchema::Validate(const ccj::Json& doc) const {
  const std::string diagnostic = Check(doc);
  if (diagnostic.empty()) {
    return Error::Ok();
  }
  if (diagnostic == kPatternBudgetDiagnostic) {
    return Error{Errc::PatternBudgetExhausted, reason::kPatternTimeout,
                 "pattern step budget exhausted"};
  }
  return Error{Errc::SchemaViolation, reason::kSchemaViolation, "schema violation", diagnostic};
}

const SchemaAudit& CompiledSchema::audit() const noexcept { return p_->audit; }

Result<std::unique_ptr<CompiledSchema>> SchemaCompiler::Compile(const ccj::Json& schema) {
  try {
    ccj::Json stripped = schema;
    SchemaAudit audit;
    SchemaAnalyzer analyzer;
    auto patterns = analyzer.Analyze(schema, stripped, audit);
    if (!patterns.ok()) {
      return patterns.error();
    }
    if (stripped.find("$schema") == stripped.end()) {
      stripped["$schema"] = std::string(kDraft7Uri);
    }

    auto compiled = std::unique_ptr<CompiledSchema>(new CompiledSchema());
    compiled->p_->validator.set_root_schema(std::move(stripped));
    compiled->p_->audit = std::move(audit);
    compiled->p_->patterns = std::move(patterns).take();
    return Result<std::unique_ptr<CompiledSchema>>{std::move(compiled)};
  } catch (...) {
    return CompileError();
  }
}

namespace testing {

Result<bool> MatcherTestSeam::DirectNfaMatch(std::string_view raw_pattern,
                                             std::string_view input_text,
                                             std::uint64_t step_budget) {
  auto program = CompilePattern(raw_pattern, true);
  if (!program.ok()) {
    return program.error();
  }
  return MatchProgram(program.value(), input_text, step_budget);
}

}  // namespace testing
}  // namespace cogito
