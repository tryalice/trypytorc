#include "torch/csrc/jit/script/compiler.h"
#include "torch/csrc/jit/generated/aten_dispatch.h"
#include "torch/csrc/jit/interpreter.h"
#include "torch/csrc/jit/ir.h"
#include "torch/csrc/jit/script/parser.h"
#include "torch/csrc/utils/object_ptr.h"

#include "ATen/optional.h"

#include <climits>

namespace torch {
namespace jit {
namespace script {

using SugaredValuePtr = std::shared_ptr<SugaredValue>;
using FunctionTable = std::unordered_map<std::string, Method&>;
using ValueTable = std::unordered_map<std::string, SugaredValuePtr>;
using AttributeMap = std::unordered_map<std::string, Const>;
using ListAttributeMap = std::unordered_map<std::string, std::vector<Const>>;

// Tuple of values. Used to implement tuple return values and unpacking
struct TupleValue : public SugaredValue {
  TupleValue(std::vector<std::shared_ptr<SugaredValue>> values) : values(std::move(values)) {}

  virtual std::string kind() const override {
    return "tuple";
  }

  virtual std::vector<std::shared_ptr<SugaredValue>> asTuple(SourceRange loc, Method& m)
      override {
    return values;
  }

 private:
  std::vector<std::shared_ptr<SugaredValue>> values;
};

// Auxiliary data structure for desugaring variable binding into our always
// explicitly scoped language as we descend down
// nested control structures in the frontend (which themselves don't introduce
// scopes)
//
// The algorithm is roughly as follows:
// 1) While emitting a block within a control operator, add inputs and outputs
//      from the block for each value referenced (both "reads" and "writes").
//      This sets the value up as a candidate loop carried dependency.
// 2) When we reach the end of the block, examine all the values in the current
//      scope's value map. If the name also resides in an outer scope with a
//      different Value*, this is a true loop-carried dependency. If not, this
//      value was not assigned to. Replace all references to the block input
//      with the Value* pointed to in the tightest enclosing scope. Then delete
//      that block input and output.
// 3) When we emit the actual control operator, take all of the loop-carried
//      dependency values as inputs and return them as outputs from the control
//      op
//
//  Note that an alternative implementation could only add the loop-carried dep
//      inputs and outputs when we see a value that is mutated. This, however
//      requires replacing all references to that value *within the current
//      block* with a new input. That is to say: we need to traverse the pre-
//      decessor nodes and replace inputs that reference that value with the
//      newly-created input. This could be made less expensive with a change to
//      the IR API, but for now we choose to pessimisitically create inputs and
//      delete unnecessary ones later with replaceAllusesWith().
struct Environment {
  Environment(Method & method, const Resolver& resolver, Block* b, std::shared_ptr<Environment> next = nullptr)
      : method(method), resolver(resolver), b(b), next(next) {}

  Method & method;
  const Resolver& resolver;
  std::vector<std::string> captured_inputs;
  Block* b;

  std::shared_ptr<Environment> next;

  SugaredValuePtr findInThisFrame(const std::string& name) {
    if (value_table.count(name)) {
      return value_table.at(name);
    }
    return nullptr;
  }

  SugaredValuePtr findInParentFrame(const std::string& name) {
    for (auto runner = next; runner; runner = runner->next) {
      if (runner->value_table.count(name)) {
        return runner->value_table.at(name);
      }
    }
    return nullptr;
  }

  Value* getValueInThisFrame(const SourceRange& loc, const std::string& name) {
    return value_table.at(name)->asValue(loc, method);
  }

  SugaredValuePtr createCapturedInput(const std::string& name) {
    // Create the input
    Value* new_input = b->addInput();

    // Associate this name with this value
    auto sv = std::make_shared<SimpleValue>(new_input);
    value_table[name] = sv;

    // List as a positional input
    captured_inputs.push_back(name);

    return sv;
  }
  Block* block() {
    return b;
  }
  Symbol getBlockOwningKind() {
    Symbol owning_kind = Symbol();
    if (b->owningNode()) {
      owning_kind = b->owningNode()->kind();
    }
    return owning_kind;
  }

  void setVar(const std::string& name, Value* value) {
    if (!findInThisFrame(name) && findInParentFrame(name) &&
        getBlockOwningKind() == prim::Loop)
      createCapturedInput(name);
    setSugaredVar(name, std::make_shared<SimpleValue>(value));
  }
  void setSugaredVar(const std::string& name, SugaredValuePtr value) {
    value_table[name] = std::move(value);
  }

  SugaredValuePtr getSugaredVar(const Ident& ident, bool required=true) {
    return getSugaredVar(ident.name(), ident.range());
  }
  Value* getVar(const Ident& ident) {
    return getSugaredVar(ident)->asValue(ident.range(), method);
  }

  SugaredValuePtr getSugaredVar(const std::string& ident, SourceRange range, bool required=true) {
    auto retval = findInThisFrame(ident);

    if (!retval && (retval = findInParentFrame(ident)) &&
        getBlockOwningKind() == prim::Loop) {
      retval = createCapturedInput(ident);
    }

    if(!retval) {
      retval = resolver(ident);
    }

    if (!retval && required) {
      throw ErrorReport(range) << "undefined value " << ident;
    }
    return retval;
  }

  Value* getVar(const std::string& ident, SourceRange range) {
    return getSugaredVar(ident, range)->asValue(range, method);
  }

  // Given that after emitting statements in a block, we've added block inputs
  // for all value references and assignments, delete inputs for which there was
  // no assignment, only references.
  void deleteExtraInputs(const SourceRange& loc, size_t skip_num = 0) {
    std::vector<size_t> inputs_to_delete;
    int i = skip_num;
    for (const auto& x : captured_inputs) {
      if (b->inputs()[i] == getValueInThisFrame(loc, x)) {
        inputs_to_delete.push_back(i);
      }
      i++;
    }

    for (auto ritr = inputs_to_delete.rbegin(); ritr != inputs_to_delete.rend();
         ++ritr) {
      auto name = captured_inputs[*ritr - skip_num];
      Value* v = getValueInThisFrame(loc, name);
      Value* orig = findInParentFrame(name)->asValue(loc, method);
      // Replace all matching node inputs with original value
      // from an enclosing scope
      v->replaceAllUsesWith(orig);

      // Actually remove the input
      b->eraseInput(*ritr);
      captured_inputs.erase(captured_inputs.begin() + *ritr - skip_num);
    }
  }
  std::vector<std::string> definedVariables() {
    std::vector<std::string> result;
    for(auto & kv : value_table) {
      result.push_back(kv.first);
    }
    return result;
  }
private:
  ValueTable value_table;
};

Node* emitBuiltinCall(
  const SourceRange& loc,
  Method& method,
  const std::string & name,
  at::ArrayRef<Value*> inputs,
  List<Attribute> attributes,
  CallsiteDescriptor cd) {
  NodeKind kind(Symbol::aten(name)); // TODO: this is a guess; could it be jit?
  auto graph = method.graph();
  auto n = graph->insertNode(graph->create(kind, inputs, cd.n_outputs))
                ->setSourceLocation(std::make_shared<SourceRange>(loc));

  for (const auto& attr : attributes) {
    const auto& name = Symbol::attr(attr.name().name());
    const Expr& value_expr = attr.value();
    switch (value_expr.kind()) {
      case TK_CONST: {
        Const value {value_expr};
        if (value.isFloatingPoint()) {
          n->f_(name, value.asFloatingPoint());
        } else {
          n->i_(name, value.asIntegral());
        }
      } break;
      case TK_LIST_LITERAL: {
        List<Const> value_list {ListLiteral(value_expr).inputs()};
        std::vector<Const> values;
        for (Const number : value_list)
          values.push_back(std::move(number));
        bool is_float = std::any_of(values.begin(), values.end(),
                                    [](const Const& c) { return c.isFloatingPoint(); });
        if (is_float) {
          n->fs_(name, fmap(values, [](const Const& c) { return c.asFloatingPoint(); }));
        } else {
          n->is_(name, fmap(values, [](const Const& c) { return c.asIntegral(); }));
        }
      } break;
    default:
        throw ErrorReport(attr) << "Unexpected kind of attribute value: " << value_expr.kind();
        break;
    }
  }

  return n;
}

std::vector<Value*> BuiltinFunction::call(
    SourceRange loc,
    Method & m,
    at::ArrayRef<Value*> inputs_,
    List<Attribute> attributes,
    CallsiteDescriptor cd) {
  std::vector<Value*> inputs;
  if (value) inputs.push_back(value);
  inputs.insert(inputs.end(), inputs_.begin(), inputs_.end());
  // TODO: remove when we support tuple packing for builtins.
  if (cd.allow_varargs && cd.n_outputs == 1) {
    cd.allow_varargs = false;
  }
  Node * n = emitBuiltinCall(loc, m, name, inputs, attributes, cd);
  if (!hasTensorOp(n)) {
    throw ErrorReport(loc) << "unknown builtin op";
  }
  if (cd.allow_varargs) {
    throw ErrorReport(loc) << "Starred packing for the output of a builtin is not supported.";
  }
  return n->outputs();
}

struct to_ir {
  to_ir(
      Def def,
      FunctionTable& function_table,
      const Resolver& resolver,
      SugaredValuePtr self,
      Method& method) // method being constructed
      : method(method)
      , graph(method.graph())
      , def(def)
      , function_table(function_table)
      , resolver(resolver)
      , environment_stack(nullptr) {
    pushFrame(graph->block());
    // inputs
    auto it = def.params().begin();
    auto end = def.params().end();
    if(self) {
      if(it == end)
        throw ErrorReport(def.params().range()) << "methods must have a self argument";
      environment_stack->setSugaredVar((*it).ident().name(), self);
      ++it;
    }
    for(;it != end; ++it) {
      auto& name = (*it).ident().name();
      environment_stack->setVar(name, graph->addInput(name));
    }
    // body
    auto stmts = def.statements();
    auto stmts_begin = stmts.begin();
    auto stmts_end = stmts.end();
    bool has_return = false;
    if (stmts_begin != stmts_end && (*std::prev(stmts_end)).kind() == TK_RETURN) {
      --stmts_end;
      has_return = true;
    }

    emitStatements(stmts_begin, stmts_end);

    // outputs
    if (has_return) {
      for (auto output : Return(*stmts_end).values()) {
        graph->registerOutput(emitExpr(output, {1, false})[0]);
      }
    }
  }

private:
  Method& method;
  std::shared_ptr<Graph> graph;
  Def def;
  FunctionTable& function_table;
  const Resolver& resolver;

  // Singly-linked list of environments. This top element contains a member
  // `next` that points to the most immediate enclosing scope's value.
  std::shared_ptr<Environment> environment_stack;

  void pushFrame(Block * b) {
    environment_stack = std::make_shared<Environment>(method, resolver, b, environment_stack);
  }
  std::shared_ptr<Environment> popFrame() {
    auto old_frame = environment_stack;
    environment_stack = environment_stack->next;
    return old_frame;
  }
  void emitStatements(const List<Stmt>& statements) {
    return emitStatements(statements.begin(), statements.end());
  }
  void emitStatements(List<Stmt>::const_iterator begin, List<Stmt>::const_iterator end) {
    for (; begin != end; ++begin) {
      auto stmt = *begin;
      switch (stmt.kind()) {
        case TK_IF:
          emitIf(If(stmt));
          break;
        case TK_WHILE:
          emitWhile(While(stmt));
          break;
        case TK_FOR:
          emitFor(For(stmt));
          break;
        case TK_ASSIGN:
          emitAssignment(Assign(stmt));
          break;
        case TK_GLOBAL:
          for (auto ident : Global(stmt).names()) {
            const auto& name = Ident(ident).name();
            environment_stack->setVar(name, graph->addInput(name));
          }
          break;
        case TK_EXPR_STMT: {
          auto exprs = ExprStmt(stmt).exprs();
          for (const auto& expr : exprs) {
            emitExpr(expr, {0, false});
          }
        }
        break;
        case TK_RETURN:
          throw ErrorReport(stmt) << "return statements can appear only at the end "
                                  << "of the function body";
          break;
      }
    }
  }

  std::shared_ptr<Environment> emitSingleIfBranch(
      Block* b,
      const List<Stmt> branch,
      std::unordered_set<std::string>* mutated_parent_values) {
    pushFrame(b);
    WithInsertPoint guard(b);
    emitStatements(branch);

    for (const auto & n : environment_stack->definedVariables()) {
      if (environment_stack->findInParentFrame(n)) {
        mutated_parent_values->insert(n);
      }
    }
    return popFrame();
  }

  Node* create(Symbol kind, const SourceRange& loc,  CallsiteDescriptor cd) {
    return graph
             ->create(kind, cd.n_outputs)
             ->setSourceLocation(std::make_shared<SourceRange>(loc));
  }

  std::vector<Value*> emitTernaryIf(const TernaryIf& expr) {
    Value* cond_value = emitExpr(expr.cond(), {1, false})[0];

    Node* n = graph->insertNode(create(prim::If, expr.range(), {0, false}));
    n->addInput(cond_value);
    auto* true_block = n->addBlock();
    auto* false_block = n->addBlock();

    auto emit_if_expr = [this](Block* b, const Expr& expr) {
      pushFrame(b);
      WithInsertPoint guard(b);
      Value* out_val = emitExpr(expr, {1, false})[0];
      b->registerOutput(out_val);
      popFrame();
    };

    emit_if_expr(true_block, expr.true_expr());
    emit_if_expr(false_block, expr.false_expr());

    // Add op outputs
    auto expr_value = n->addOutput(); // Resulting value

    return {expr_value};
  }

  void emitIf(const If& stmt) {
    Value* cond_value = emitExpr(stmt.cond(), {1, false})[0];

    Node* n = graph->insertNode(create(prim::If, stmt.range(), {0, false}));
    n->addInput(cond_value);
    auto* true_block = n->addBlock();
    auto* false_block = n->addBlock();

    // Emit both blocks once to get the union of all mutated values
    std::unordered_set<std::string> mutated_parent_values;
    auto save_true = emitSingleIfBranch(
        true_block, stmt.trueBranch(), &mutated_parent_values);
    auto save_false = emitSingleIfBranch(
        false_block, stmt.falseBranch(), &mutated_parent_values);

    std::vector<std::string> sorted_mutations(
        mutated_parent_values.begin(), mutated_parent_values.end());
    std::sort(sorted_mutations.begin(), sorted_mutations.end());

    // Register outputs in each block
    for (const auto& x : sorted_mutations) {
      true_block->registerOutput(save_true->getVar(x, stmt.range()));
    }
    for (const auto& x : sorted_mutations) {
      false_block->registerOutput(save_false->getVar(x, stmt.range()));
    }

    // Add op outputs
    for (const auto& x : sorted_mutations) {
      environment_stack->setVar(x, n->addOutput());
    }
  }

  // *********************** Loop Operators ************************************
  // Emits a loop operators conforming to the semantics specified at
  // https://github.com/onnx/onnx/blob/master/docs/Operators.md#experimental-loop
  // TODO: implement scan_outputs

  // the format of the Loop instruction is:
  // loop_carried_outputs* = Loop(max_trip_count, start_condition,
  // loop_carried_inputs*)
  //                          block0(loop_counter, loop_carried_block*) {
  //                             <body>
  //                             -> (continue_condition,
  //                             loop_carried_block_outputs*)
  //                          }
  // all loop_carried_... lists are the same length and represent the value of
  // loop-carried variables whose definitions are updated as the loop executes
  // in a way that ensure single static assignment.

  void emitLoopCommon(
      SourceRange range,
      at::optional<Expr> max_trip_count,
      at::optional<Expr> cond,
      const List<Stmt>& body,
      at::optional<Ident> itr_ident) {
    Node* n = graph->insertNode(create(prim::Loop, range, {0, false}));
    Value *max_trip_count_val, *cond_val;
    {
      WithInsertPoint guard(n);
      if (max_trip_count) {
        max_trip_count_val = emitExpr(max_trip_count.value(), {1, false})[0];
      } else {
        max_trip_count_val =
            emitConst(Const::create(range, std::to_string(INT_MAX)))[0];
      }
      if (cond) {
        cond_val = emitExpr(cond.value(), {1, false})[0];
      } else {
        cond_val = emitBooleanConst(range, true)[0];
      }
    }
    n->addInput(max_trip_count_val);
    n->addInput(cond_val);
    auto* body_block = n->addBlock();
    Value* trip_count = body_block->addInput(); // Iteration num
    size_t skip_inputs_num = 1;

    {
      pushFrame(body_block);
      if (itr_ident) {
        environment_stack->setVar(itr_ident.value().name(), trip_count);
      }
      WithInsertPoint guard(body_block);
      emitStatements(body);

      // Also emit the conditional
      if (cond) {
        Value* body_cond_value = emitExpr(cond.value(), {1, false})[0];
        body_block->registerOutput(body_cond_value);
      } else {
        Value* cond_value_dummy = emitBooleanConst(range, true)[0];
        body_block->registerOutput(cond_value_dummy);
      }

      auto body_frame = popFrame();
      auto outer_frame = environment_stack;
      // Remove inputs for values that did not mutate within the
      // block
      body_frame->deleteExtraInputs(range, skip_inputs_num);

      // Add block outputs
      for (const auto& x : body_frame->captured_inputs) {
        body_block->registerOutput(body_frame->getValueInThisFrame(range, x));
        n->addInput(outer_frame->getVar(x, range));
        outer_frame->setVar(x, n->addOutput());
      }

    }
  }

  void emitForRange(SourceRange range, const Ident& target, const List<Expr>& args, const List<Stmt>& body) {
    // TODO: start, stop, step loop
    if (args.size() != 1) {
      throw ErrorReport(range)
          << "range() expects one argument but got" << args.size();
    }
    emitLoopCommon(range, {args[0]}, {}, body, target);
  }

  void emitFor(const For& stmt) {
    // For now, we only support range loops. e.g. for i in range(3): ...
    auto targets = stmt.targets();
    auto itrs = stmt.itrs();
    auto body = stmt.body();

    if (stmt.itrs().size() != 1) {
      throw ErrorReport(stmt)
          << "List of iterables is not supported currently.";
    }
    if (targets.size() != 1) {
      throw ErrorReport(stmt) << "Iteration variable unpacking is not supported";
    }

    if (targets[0].kind() != TK_VAR) {
      throw ErrorReport(targets[0]) << "Starred unpacking is currently not"
          << " supported for for loops.";
    }
    auto target = Var(targets[0]).name();

    // match range(<expr>) style loops
    // itrs must consist of a single Apply node
    if (itrs[0].kind() == TK_APPLY) {
      Apply range_iterator = Apply(itrs[0]);
      if (range_iterator.callee().kind() == TK_VAR) {
        Var var = Var(range_iterator.callee());
        if (var.name().name() == "range") {
          return emitForRange(stmt.range(), target, range_iterator.inputs(), body);
        }
      }
    }

    // it isn't a range(<expr>) loop, treat it as a sugared value that maybe can be
    // unrolled
    auto sv = emitSugaredExpr(itrs[0]);
    auto instances = sv->asTuple(stmt.range(), method);
    const std::string& target_name = target.name();
    pushFrame(environment_stack->block());
    for(auto inst : instances) {
      environment_stack->setSugaredVar(target_name, inst);
      emitStatements(body);
    }

    for (const auto & n : environment_stack->definedVariables()) {
      if (environment_stack->findInParentFrame(n)) {
        environment_stack->next->setVar(n, environment_stack->getVar(n, stmt.range()));
      }
    }
    popFrame();
  }

  void emitWhile(const While& stmt) {
    auto cond = stmt.cond();
    emitLoopCommon(stmt.range(), {}, {cond}, stmt.body(), {});
  }

  // Validate that the `lhs` Expr's in an assignment statement are valid. That
  // is:
  //
  // 1) All lhs Expr's are either Var or Starred nodes
  // 2) There is at most one Starred node in the lhs Expr
  // 3) A Starred node can only appear when there is another non-Starred lhs Expr
  //    Concretely this means that `*abc = func()` is illegal. Unpacking all
  //    outputs into a tuple is covered by `abc = func()`.
  bool calcNumStarredUnpack(const List<Expr>& lhs, const SourceRange& r) {
    size_t num_normal_assign = 0;
    size_t num_starred = 0;
    for (const auto& assignee : lhs) {
      if (assignee.kind() == TK_VAR) {
        num_normal_assign++;
      } else if (assignee.kind() == TK_STARRED) {
        num_starred++;
      } else {
        throw ErrorReport(assignee)
            << "lhs of assignment must be a variable or starred expression.";
      }
    }

    if (num_starred > 1) {
      throw ErrorReport(r)
          << "Only one starred expression is allowed on the lhs.";
    }

    if (num_starred > 0 && num_normal_assign == 0) {
      throw ErrorReport(r) << "A Starred expression may only appear on the "
                              << "lhs within the presence of another non-starred"
                              << " expression.";
    }

    return num_starred;
  }

  std::vector<SugaredValuePtr> createSugaredValuesFromValues(const std::vector<Value*> values) {
    std::vector<SugaredValuePtr> sugared_outputs;
    sugared_outputs.reserve(values.size());
    for (Value* output : values) {
      sugared_outputs.emplace_back(std::make_shared<SimpleValue>(output));
    }
    return sugared_outputs;
  }

  std::vector<Value*> emitAssignment(const Assign& stmt) {
    std::vector<Value*> outputs{stmt.lhs().size()};
    bool starred_unpack = calcNumStarredUnpack(stmt.lhs(), stmt.range());
    if (stmt.reduction() != '=') {
      if (stmt.lhs().size() != 1) {
        throw ErrorReport(stmt)
            << "reductions are only allowed when there is a single variable "
            << "on the left-hand side.";
      }
      Ident lhs = Var(stmt.lhs()[0]).name();
      Expr expr = BinOp::create(stmt.range(), stmt.reduction(),
                                Var::create(lhs.range(), lhs), stmt.rhs());
      outputs = emitExpr(expr, {1, false});
    } else {
      CallsiteDescriptor cd{stmt.lhs().size(), starred_unpack || stmt.lhs().size() == 1};
      outputs =
          emitExpr(stmt.rhs(), cd);
    }
    if (stmt.lhs().size() == 1 && outputs.size() != 1) {
      // Pack up a tuple sugared value
      SugaredValuePtr tup = std::make_shared<TupleValue>(createSugaredValuesFromValues(outputs));
      if (stmt.lhs()[0].kind() != TK_VAR) {
        throw ErrorReport(stmt.lhs()[0]) << "Cannot pack a tuple into a non-variable.";
      }
      environment_stack->setSugaredVar(Var(stmt.lhs()[0]).name().name(), tup);
    } else {
      int i = 0;
      for (auto assignee : stmt.lhs()) {
        if (assignee.kind() == TK_VAR) {
          environment_stack->setVar(Var(assignee).name().name(), outputs.at(i));
          i++;
        } else if (assignee.kind() == TK_STARRED) {
          auto var = Starred(assignee).expr();
          if (var.kind() != TK_VAR) {
            throw ErrorReport(var) << "Cannot pack a tuple into a non-variable.";
          }
          std::vector<Value*> starred_slice(
              outputs.begin() + i, outputs.begin() + i + (starred_unpack ? 1 : 0));
          SugaredValuePtr tup = std::make_shared<TupleValue>(createSugaredValuesFromValues(starred_slice));
          environment_stack->setSugaredVar(
              Var(Starred(assignee).expr()).name().name(), tup);
          i += starred_unpack ? 1 : 0;
        }
      }
    }
    return outputs;
  }

  NodeKind getNodeKind(int kind, int ninputs) {
    switch (kind) {
      case '+':
        return aten::add;
      case '-':
        return aten::sub;
      case TK_UNARY_MINUS:
        return aten::neg;
      case '*':
        return aten::mul;
      case TK_STARRED:
        return prim::Starred;
      case '/':
        return aten::div;
      case TK_NE:
        return aten::ne;
      case TK_EQ:
        return aten::eq;
      case '<':
        return aten::lt;
      case '>':
        return aten::gt;
      case TK_LE:
        return aten::le;
      case TK_GE:
        return aten::ge;
      case TK_AND:
        return aten::__and__;
      case TK_OR:
        return aten::__or__;
      case TK_NOT:
        return aten::__not__;
      default:
        throw std::runtime_error("unknown kind " + std::to_string(kind));
    }
  }

  template <typename Trees>
  std::vector<Value*> getValues(const Trees& trees, bool maybe_unpack=false) {
    std::vector<Value*> values;
    for (const auto& tree : trees) {
      CallsiteDescriptor cd{1, maybe_unpack};
      auto outputs = emitExpr(tree, cd);
      if (!maybe_unpack && outputs.size() > 1) {
        throw ErrorReport(tree) << "Expr unexpectedly returned more than 1 value."
                                << " File a bug report.";
      }
      for (auto* v : outputs) {
        values.push_back(v);
      }
    }
    return values;
  }

  void expectOutputs(
      const TreeRef& tree,
      const size_t expected_size,
      const size_t size) {
    if (expected_size != 0 && expected_size != size) {
      throw ErrorReport(tree)
          << "expected operator to produce " << expected_size
          << " outputs but it produced " << size;
    }
  }

  // special rules apply when we directly call foo(a,b) when foo is an ident
  std::vector<Value*> emitApplyIdent(Ident ident, std::vector<Value*> inputs, List<Attribute> attributes, CallsiteDescriptor cd) {
    auto it = function_table.find(ident.name());
    if (it != function_table.end()) {
      if(inputs.size() != it->second.num_inputs())
        throw ErrorReport(ident) << "expected " << it->second.num_inputs() << " but found " << inputs.size();
      auto outputs = method.emit_call_to(it->second, inputs);
      if (!cd.allow_varargs)
        expectOutputs(ident, cd.n_outputs, outputs.size());
      return outputs;
    } else if (ident.name() == "print") {
      expectOutputs(ident, cd.n_outputs, 0);
      if (!attributes.empty())
        throw ErrorReport(ident) << "print doesn't accept any keyword arguments";
      return emitNode(prim::Print, ident.range(), inputs, {0, false})->outputs();
    }
    // TODO: remove when we can support tuple packing for builtins.
    if (cd.allow_varargs && cd.n_outputs == 1) {
      cd.allow_varargs = false;
    }
    Node* builtin = emitBuiltinCall(ident.range(), method, ident.name(), inputs, attributes, cd);
    if (hasTensorOp(builtin)) {
      if (cd.allow_varargs) {
        throw ErrorReport(ident.range()) << "Starred assignment isn't supported on builtins.";
      }
      return builtin->outputs();
    }
    builtin->destroy();
    // it wasn't known built in, so treat it like standard apply
    return emitApplyExpr(Var::create(ident.range(), ident), inputs, attributes, cd);
  }

  std::vector<Value*> emitApplyExpr(Expr callee, const std::vector<Value*>& inputs, List<Attribute> attributes, CallsiteDescriptor cd) {
    // otherwise we evaluate the callee and then desugar it
    auto sv = emitSugaredExpr(callee);
    return sv->call(callee.range(), method, inputs, attributes, cd);
  }

  // any expression that can produce a SugaredValue is handled here
  // with emitExpr falling back to this function to handle them
  // the kinds handled here should be kept in sync with [SUGARED VALUES]
  // in emitExpr
  std::shared_ptr<SugaredValue> emitSugaredExpr(Expr tree) {
    switch(tree.kind()) {
      case TK_VAR:
        return environment_stack->getSugaredVar(Var(tree).name());
      case '.': {
        auto select = Select(tree);
        auto sv = emitSugaredExpr(select.value());
        return sv->attr(select.range(), method, select.selector().name());
      }
      default:
        return std::make_shared<SimpleValue>(emitExpr(tree, {1, false})[0]);
    }
  }

  std::vector<Value*> emitExpr(
      const TreeRef& tree,
      CallsiteDescriptor cd) {
    switch (tree->kind()) {
      // the expressions have special handling because they may operate
      // on sugared values
      // [SUGARED VALUES]
      case TK_VAR: case '.': {
        return { emitSugaredExpr(Expr(tree))->asValue(tree->range(), method) };
      } break;
      case TK_NE:
      case TK_EQ:
      case '<':
      case '>':
      case TK_LE:
      case TK_GE:
      case '*':
      case '/':
      case TK_AND:
      case TK_OR:
      case TK_NOT:
      case TK_UNARY_MINUS: {
        expectOutputs(tree, cd.n_outputs, 1);
        const auto& inputs = tree->trees();
        auto kind = getNodeKind(tree->kind(), inputs.size());
        return emitNode(kind, tree->range(), getValues(inputs), cd)->outputs();
      } break;
      case '+':
      case '-': {
        expectOutputs(tree, cd.n_outputs, 1);
        const auto& inputs = tree->trees();
        auto kind = getNodeKind(tree->kind(), inputs.size());
        auto* node = emitNode(kind, tree->range(), getValues(inputs), cd);
        node->t_(Symbol::attr("alpha"), at::CPU(at::kFloat).scalarTensor(1.0));
        return node->outputs();
      }
      case TK_STARRED: {
        const auto starred = Starred(tree);
        auto sugared = emitSugaredExpr(starred.expr());
        auto sugared_retvals = sugared->asTuple(starred.range(), environment_stack->method);
        std::vector<Value*> retvals;
        retvals.reserve(sugared_retvals.size());
        for (auto & val : sugared_retvals) {
          retvals.push_back(val->asValue(starred.range(), method));
        }
        return retvals;
      }
      case TK_APPLY: {
        auto apply = Apply(tree);
        auto inputs = getValues(apply.inputs(), true);
        // the apply is directly an identifier 'foo'
        if(apply.callee().kind() == TK_VAR) {
          return emitApplyIdent(Var(apply.callee()).name(), inputs, apply.attributes(), cd);
        }
        return emitApplyExpr(apply.callee(), inputs, apply.attributes(), cd);
      } break;
      case TK_CAST: {
        expectOutputs(tree, cd.n_outputs, 1);
        const auto cast = Cast(tree);
        return emitCast(cast.input(), cast.type());
      } break;
      case TK_CONST: {
        expectOutputs(tree, cd.n_outputs, 1);
        return emitConst(Const(tree));
      } break;
      case TK_TRUE: {
        return emitBooleanConst(tree->range(), true);
      } break;
      case TK_FALSE: {
        return emitBooleanConst(tree->range(), false);
      } break;
      case TK_SLICE: {
        expectOutputs(tree, cd.n_outputs, 1);
        const auto slice = Slice(tree);
        return emitSlice(
            slice.range(),
            {slice.value(), slice.startOr(0), slice.endOr(-1)},
            cd);
      } break;
      case TK_GATHER: {
        expectOutputs(tree, cd.n_outputs, 1);
        const auto gather = Gather(tree);
        return emitGather(
            gather.range(), {gather.value(), gather.indices()}, cd);
      } break;
      case TK_IF_EXPR: {
        expectOutputs(tree, cd.n_outputs, 1);
        return emitTernaryIf(TernaryIf(tree));
      } break;
      default:
        throw ErrorReport(tree) << "NYI: " << tree;
        break;
    }
  }

  std::vector<Value*> emitCast(const TreeRef& input, const ScalarType& type) {
    at::ScalarType t;
    switch (type.kind()) {
      case TK_INT:
        t = at::kInt;
        break;
      case TK_FLOAT:
        t = at::kFloat;
        break;
      case TK_LONG:
        t = at::kLong;
        break;
      case TK_BOOL:
        t = at::kByte;
        break;
      default:
        throw ErrorReport(input) << "Unrecognized type: " << type;
    }
    return emitNode(
               Symbol::aten("type_as"),
               input->range(),
               {emitExpr(input, {1, false})[0], createConstant(input->range(), at::ones(at::CPU(t), {1}))},
               {1, false})
        ->outputs();
  }

  std::vector<Value*> emitBooleanConst(SourceRange range, bool val) {
    return {createConstant(range, at::CPU(at::kByte).scalarTensor(val))};
  }

  std::vector<Value*> emitConst(const Const& c) {
    if (c.isFloatingPoint()) {
      return {createConstant(c.range(), at::CPU(at::kFloat).scalarTensor(c.asFloatingPoint()))};
    } else {
      return {createConstant(c.range(), at::CPU(at::kLong).scalarTensor(c.asIntegral()))};
    }
  }

  Node* emitNode(
      NodeKind kind,
      const SourceRange& loc,
      const std::vector<Value*> inputs,
      CallsiteDescriptor cs) {
    Node* n = graph->insertNode(create(kind, loc, cs));
    for (auto* input_value : inputs) {
      n->addInput(input_value);
    }
    return n;
  }

  // Desugars slice syntactic sugar tensor[begin:end] -> tensor.slice(begin,
  // end).
  std::vector<Value*> emitSlice(
      const SourceRange& loc,
      TreeList&& inputs,
      CallsiteDescriptor cs) {
    const auto applyInputs =
        Compound::create(TK_LIST, loc, std::move(inputs));
    const auto input_values = getValues(applyInputs->trees());
    Value* tensor = input_values[0];
    const auto& begin = at::Scalar(input_values[1]->node()->t(attr::value)).toInt();
    const auto& end = at::Scalar(input_values[2]->node()->t(attr::value)).toInt();
    return emitNode(
               Symbol::aten("slice"),
               loc,
               {tensor},
               cs)
               ->i_(attr::dim, 0)
               ->i_(attr::step, 1)
               ->i_(attr::start, begin)
               ->i_(attr::end, end)->outputs();
  }

  // Desugars gather syntactic sugar tensor[idx] -> tensor.select(idx).
  std::vector<Value*> emitGather(
      const SourceRange& loc,
      TreeList&& inputs,
      CallsiteDescriptor cs) {
    const auto applyInputs =
        Compound::create(TK_LIST, loc, std::move(inputs));
    const auto input_values = getValues(applyInputs->trees());
    Value* tensor = input_values[0];
    const auto& idx = at::Scalar(input_values[1]->node()->t(attr::value)).toInt();
    return emitNode(
               Symbol::aten("select"),
               loc,
               {tensor},
               cs)
               ->i_(attr::dim, 0)
               ->i_(attr::index, idx)
               ->outputs();
  }

  Value* createConstant(const SourceRange& loc, const at::Tensor& val) {
    auto n = graph->createConstant(val);
    n->setSourceLocation(std::make_shared<SourceRange>(loc));
    return graph->insertNode(n)->output();
  }
};

// support syntax sugar for x.foo(y, z) by allowing x.foo to return a
// callable value that will resolve to foo(x, y, z) when called.
std::shared_ptr<SugaredValue> SimpleValue::attr(SourceRange loc, Method & m, const std::string& field) {
  return std::make_shared<BuiltinFunction>(field, value);
}


void defineMethodsInModule(Module & m, const std::vector<Def>& definitions, const Resolver& resolver, SugaredValuePtr self) {
  FunctionTable table;
  for(auto def : definitions) {
    const std::string& name = def.name().name();
    Method& method = m.create_method(name);
    to_ir(def, table, resolver, self,  method);
    auto result = table.emplace(name, method);
    if(!result.second) {
      throw ErrorReport(def) << "duplicate definition of function '" << name << "'";
    }
  }
}

void defineMethodsInModule(Module & m, const std::string& source, const Resolver& resolver, SugaredValuePtr self) {
  Parser p(source);
  std::vector<Def> definitions;
  while (p.lexer().cur().kind != TK_EOF) {
    definitions.push_back(Def(p.parseFunction()));
  }
  defineMethodsInModule(m, definitions, resolver, self);
}

std::shared_ptr<Graph> compileFunction(Def def, const Resolver& resolver) {
  Module m; //note: we don't use 'm' to execute so this setting is unused
  defineMethodsInModule(m, {def}, resolver, nullptr);
  return m.get_method(def.name().name()).graph();
}

} // namespace script
} // namespace jit
} // namespace torch
