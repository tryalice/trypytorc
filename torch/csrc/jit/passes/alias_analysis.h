#pragma once

#include <torch/csrc/jit/alias_info.h>
#include <torch/csrc/jit/ir.h>

namespace torch {
namespace jit {
class AliasSetTracker;

/**
 * Alias analysis pass.
 *
 * This pass produces an AliasDb that contains aliasing and mutation
 * information about the graph. Users can use this information to determine
 * whether mutations to the graph are safe, i.e. they don't reorder/change
 * nodes in a way that affects output.
 *
 * Every value with a mutable type (Tensors, Lists, Tuples, etc.) will be
 * associated with one or more "alias sets". If two values share an alias set,
 * that means they may alias, implying that a mutation to one value cannot be
 * reordered past a use of the other. Only reordering two reads of an alias set
 * is considered safe.
 *
 * There is a special alias set called the "wildcard set", which indicates that
 * we're not sure what this value may alias. To be conservative, we consider
 * the wildcard alias set as potentially aliasing any value.
 */
class AliasDb {
 public:
  TORCH_API explicit AliasDb(std::shared_ptr<Graph> graph);
  TORCH_API ~AliasDb();

  // Does `n` write to any alias sets?
  bool hasWrites(Node* n) const;

  // There are limitations to what effects the alias analysis can track. Two
  // kinds of nodes may have untracked effects:
  // 1. Nodes that write to a value that may alias the graph inputs (since
  //    the inputs can be used outside the graph).
  // 2. Nodes that write to something in the wildcard set.
  //
  // These nodes are considered not safe to eliminate or mutate under any
  // circumstances.
  bool hasUntrackedEffects(Node* n) const;

  // Get all the values that `n` writes to.
  std::unordered_set<const Value*> getWrites(Node* n) const;

  // Get all values that may alias to `v`.
  std::unordered_set<const Value*> getAliases(const Value* v) const;

  // Do any nodes write to an alias set inputed/outputed by `n`?
  bool hasWriters(const Node* n) const;

  // Same as hasWriters() but ignores writes after `n`.
  bool hasWritersBefore(const Node* n) const;

  // Move 'n' (already in the graph) after 'movePoint' in the topological order.
  //
  // Tries to preserve value dependencies, so other nodes might be moved. We
  // make two gurantees about the postcondition of the node list:
  //   - `n` is directly after `movePoint`.
  //   - only nodes between `n` and `movePoint` have been moved.
  //
  // Returns `false` if it's impossible to move `n` after `MovePoint` without
  // violating dependencies, otherwise executes the move and returns `true`
  bool moveAfterTopologicallyValid(Node* n, Node* movePoint);
  bool moveBeforeTopologicallyValid(Node* n, Node* movePoint);

  bool couldMoveAfterTopologically(Node* n, Node* movePoint);
  bool couldMoveBeforeTopologically(Node* n, Node* movePoint);

  // Returns true if `n` is safe to be deinplaced.
  bool canDeinplace(Node* n) const;

  // De-inplace this node, turning it into a pure operator (e.g. add_ -> add)
  // and rewriting subsequent uses of `n`'s output to use the result.
  //
  // Returns the new deinplaced node.
  Node* deinplace(Node* n);

  // For debugging: print alias db state to stdout
  TORCH_API void dump() const;

 private:
  // Helper for topologically-safe node moves.
  class WorkingSet;
  enum class MoveSide { BEFORE, AFTER };
  bool tryMove(Node* toMove, Node* movePoint, MoveSide moveSide, bool dryRun);
  void move(Node* toMove, Node* movePoint, MoveSide moveSide);
  bool isBeforeOrAfter(const Node* n, MoveSide moveSide) const;

  // Does `n` use or write to any wildcard aliases?
  bool hasWildcard(const Node* n) const;
  // Returns nullopt if there are no wildcard nodes
  c10::optional<const Node*> getLastWildcard() const;

  // Get all nodes that write to any alias set inputed/outputed by `n`
  std::unordered_set<Node*> getWriters(const Node* n) const;

  // Does `n` write to a value that may alias one of the graph inputs?
  bool writesToInputAlias(Node* n) const;

  // Update API for the AliasDB.
  void insert(Node* node);
  void erase(Node* node);

  void analyze(const std::shared_ptr<Graph>& graph);
  void analyze(Block* block);
  void analyze(Node* node);
  void analyzeImpl(Node* node);

  void analyzeIf(Node* node);
  void analyzeLoop(Node* node);
  void analyzeSubgraph(Node* node);
  void analyzeCreator(Node* node);
  void analyzeExtractor(Node* node);
  void analyzeChunk(Node* node);
  void analyzeBroadcastingChunk(Node* node);

  bool addAlias(const Value* value, const Value* from);
  void mapAliases(at::ArrayRef<Value*> to, at::ArrayRef<Value*> from);
  void giveFreshAlias(const Value* value);

  bool hasUsesAfter(Symbol alias, const Node* n) const;
  bool writesTo(Node* n, const Value* v) const;
  bool isBeforeSameGraph(const Node* lhs, const Node* rhs) const;

  std::shared_ptr<Graph> graph_;
  std::unordered_map<const Graph*, const Node*> subgraphToOwner_;
  std::unordered_set<const Node*> wildcardNodes_;
  std::unique_ptr<AliasSetTracker> setTracker_;
};
} // namespace jit
} // namespace torch
