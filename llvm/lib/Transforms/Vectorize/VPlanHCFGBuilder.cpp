//===-- VPlanHCFGBuilder.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the construction of a VPlan-based Hierarchical CFG
/// (H-CFG) for an incoming IR. This construction comprises the following
/// components and steps:
//
/// 1. PlainCFGBuilder class: builds a plain VPBasicBlock-based CFG that
/// faithfully represents the CFG in the incoming IR. A VPRegionBlock (Top
/// Region) is created to enclose and serve as parent of all the VPBasicBlocks
/// in the plain CFG.
/// NOTE: At this point, there is a direct correspondence between all the
/// VPBasicBlocks created for the initial plain CFG and the incoming
/// BasicBlocks. However, this might change in the future.
///
//===----------------------------------------------------------------------===//

#include "VPlanHCFGBuilder.h"
#include "LoopVectorizationPlanner.h"
#include "VPlanCFG.h"
#include "llvm/Analysis/LoopIterator.h"

#define DEBUG_TYPE "loop-vectorize"

using namespace llvm;

namespace {
// Class that is used to build the plain CFG for the incoming IR.
class PlainCFGBuilder {
private:
  // The outermost loop of the input loop nest considered for vectorization.
  Loop *TheLoop;

  // Loop Info analysis.
  LoopInfo *LI;

  // Vectorization plan that we are working on.
  VPlan &Plan;

  // Builder of the VPlan instruction-level representation.
  VPBuilder VPIRBuilder;

  // NOTE: The following maps are intentionally destroyed after the plain CFG
  // construction because subsequent VPlan-to-VPlan transformation may
  // invalidate them.
  // Map incoming BasicBlocks to their newly-created VPBasicBlocks.
  DenseMap<BasicBlock *, VPBasicBlock *> BB2VPBB;
  // Map incoming Value definitions to their newly-created VPValues.
  DenseMap<Value *, VPValue *> IRDef2VPValue;

  // Hold phi node's that need to be fixed once the plain CFG has been built.
  SmallVector<PHINode *, 8> PhisToFix;

  /// Maps loops in the original IR to their corresponding region.
  DenseMap<Loop *, VPRegionBlock *> Loop2Region;

  // Utility functions.
  void setVPBBPredsFromBB(VPBasicBlock *VPBB, BasicBlock *BB);
  void setRegionPredsFromBB(VPRegionBlock *VPBB, BasicBlock *BB);
  void fixHeaderPhis();
  VPBasicBlock *getOrCreateVPBB(BasicBlock *BB);
#ifndef NDEBUG
  bool isExternalDef(Value *Val);
#endif
  VPValue *getOrCreateVPOperand(Value *IRVal);
  void createVPInstructionsForVPBB(VPBasicBlock *VPBB, BasicBlock *BB);

public:
  PlainCFGBuilder(Loop *Lp, LoopInfo *LI, VPlan &P)
      : TheLoop(Lp), LI(LI), Plan(P) {}

  /// Build plain CFG for TheLoop  and connects it to Plan's entry.
  void buildPlainCFG(DenseMap<VPBlockBase *, BasicBlock *> &VPB2IRBB);
};
} // anonymous namespace

// Set predecessors of \p VPBB in the same order as they are in \p BB. \p VPBB
// must have no predecessors.
void PlainCFGBuilder::setVPBBPredsFromBB(VPBasicBlock *VPBB, BasicBlock *BB) {
  auto GetLatchOfExit = [this](BasicBlock *BB) -> BasicBlock * {
    auto *SinglePred = BB->getSinglePredecessor();
    Loop *LoopForBB = LI->getLoopFor(BB);
    if (!SinglePred || LI->getLoopFor(SinglePred) == LoopForBB)
      return nullptr;
    // The input IR must be in loop-simplify form, ensuring a single predecessor
    // for exit blocks.
    assert(SinglePred == LI->getLoopFor(SinglePred)->getLoopLatch() &&
           "SinglePred must be the only loop latch");
    return SinglePred;
  };
  if (auto *LatchBB = GetLatchOfExit(BB)) {
    auto *PredRegion = getOrCreateVPBB(LatchBB)->getParent();
    assert(VPBB == cast<VPBasicBlock>(PredRegion->getSingleSuccessor()) &&
           "successor must already be set for PredRegion; it must have VPBB "
           "as single successor");
    VPBB->setPredecessors({PredRegion});
    return;
  }
  // Collect VPBB predecessors.
  SmallVector<VPBlockBase *, 2> VPBBPreds;
  for (BasicBlock *Pred : predecessors(BB))
    VPBBPreds.push_back(getOrCreateVPBB(Pred));
  VPBB->setPredecessors(VPBBPreds);
}

static bool isHeaderBB(BasicBlock *BB, Loop *L) {
  return L && BB == L->getHeader();
}

void PlainCFGBuilder::setRegionPredsFromBB(VPRegionBlock *Region,
                                           BasicBlock *BB) {
  // BB is a loop header block. Connect the region to the loop preheader.
  Loop *LoopOfBB = LI->getLoopFor(BB);
  Region->setPredecessors({getOrCreateVPBB(LoopOfBB->getLoopPredecessor())});
}

// Add operands to VPInstructions representing phi nodes from the input IR.
void PlainCFGBuilder::fixHeaderPhis() {
  for (auto *Phi : PhisToFix) {
    assert(IRDef2VPValue.count(Phi) && "Missing VPInstruction for PHINode.");
    VPValue *VPVal = IRDef2VPValue[Phi];
    assert(isa<VPWidenPHIRecipe>(VPVal) &&
           "Expected WidenPHIRecipe for phi node.");
    auto *VPPhi = cast<VPWidenPHIRecipe>(VPVal);
    assert(VPPhi->getNumOperands() == 0 &&
           "Expected VPInstruction with no operands.");

    Loop *L = LI->getLoopFor(Phi->getParent());
    assert(isHeaderBB(Phi->getParent(), L));
    // For header phis, make sure the incoming value from the loop
    // predecessor is the first operand of the recipe.
    assert(Phi->getNumOperands() == 2 &&
           "header phi must have exactly 2 operands");
    BasicBlock *LoopPred = L->getLoopPredecessor();
    VPPhi->addOperand(
        getOrCreateVPOperand(Phi->getIncomingValueForBlock(LoopPred)));
    BasicBlock *LoopLatch = L->getLoopLatch();
    VPPhi->addOperand(
        getOrCreateVPOperand(Phi->getIncomingValueForBlock(LoopLatch)));
  }
}

static bool isHeaderVPBB(VPBasicBlock *VPBB) {
  return VPBB->getParent() && VPBB->getParent()->getEntry() == VPBB;
}

/// Return true of \p L loop is contained within \p OuterLoop.
static bool doesContainLoop(const Loop *L, const Loop *OuterLoop) {
  if (L->getLoopDepth() < OuterLoop->getLoopDepth())
    return false;
  const Loop *P = L;
  while (P) {
    if (P == OuterLoop)
      return true;
    P = P->getParentLoop();
  }
  return false;
}

// Create a new empty VPBasicBlock for an incoming BasicBlock in the region
// corresponding to the containing loop  or retrieve an existing one if it was
// already created. If no region exists yet for the loop containing \p BB, a new
// one is created.
VPBasicBlock *PlainCFGBuilder::getOrCreateVPBB(BasicBlock *BB) {
  if (auto *VPBB = BB2VPBB.lookup(BB)) {
    // Retrieve existing VPBB.
    return VPBB;
  }

  // Create new VPBB.
  StringRef Name = isHeaderBB(BB, TheLoop) ? "vector.body" : BB->getName();
  LLVM_DEBUG(dbgs() << "Creating VPBasicBlock for " << Name << "\n");
  VPBasicBlock *VPBB = Plan.createVPBasicBlock(Name);
  BB2VPBB[BB] = VPBB;

  // Get or create a region for the loop containing BB, except for the top
  // region of TheLoop which is created later.
  Loop *LoopOfBB = LI->getLoopFor(BB);
  if (!LoopOfBB || LoopOfBB == TheLoop || !doesContainLoop(LoopOfBB, TheLoop))
    return VPBB;

  auto *RegionOfVPBB = Loop2Region.lookup(LoopOfBB);
  if (!isHeaderBB(BB, LoopOfBB)) {
    assert(RegionOfVPBB &&
           "Region should have been created by visiting header earlier");
    VPBB->setParent(RegionOfVPBB);
    return VPBB;
  }

  assert(!RegionOfVPBB &&
         "First visit of a header basic block expects to register its region.");
  // Handle a header - take care of its Region.
  RegionOfVPBB = Plan.createVPRegionBlock(Name.str(), false /*isReplicator*/);
  RegionOfVPBB->setParent(Loop2Region[LoopOfBB->getParentLoop()]);
  RegionOfVPBB->setEntry(VPBB);
  Loop2Region[LoopOfBB] = RegionOfVPBB;
  return VPBB;
}

#ifndef NDEBUG
// Return true if \p Val is considered an external definition. An external
// definition is either:
// 1. A Value that is not an Instruction. This will be refined in the future.
// 2. An Instruction that is outside of the CFG snippet represented in VPlan,
// i.e., is not part of: a) the loop nest, b) outermost loop PH and, c)
// outermost loop exits.
bool PlainCFGBuilder::isExternalDef(Value *Val) {
  // All the Values that are not Instructions are considered external
  // definitions for now.
  Instruction *Inst = dyn_cast<Instruction>(Val);
  if (!Inst)
    return true;

  BasicBlock *InstParent = Inst->getParent();
  assert(InstParent && "Expected instruction parent.");

  // Check whether Instruction definition is in loop PH.
  BasicBlock *PH = TheLoop->getLoopPreheader();
  assert(PH && "Expected loop pre-header.");

  if (InstParent == PH)
    // Instruction definition is in outermost loop PH.
    return false;

  // Check whether Instruction definition is in a loop exit.
  SmallVector<BasicBlock *> ExitBlocks;
  TheLoop->getExitBlocks(ExitBlocks);
  if (is_contained(ExitBlocks, InstParent)) {
    // Instruction definition is in outermost loop exit.
    return false;
  }

  // Check whether Instruction definition is in loop body.
  return !TheLoop->contains(Inst);
}
#endif

// Create a new VPValue or retrieve an existing one for the Instruction's
// operand \p IRVal. This function must only be used to create/retrieve VPValues
// for *Instruction's operands* and not to create regular VPInstruction's. For
// the latter, please, look at 'createVPInstructionsForVPBB'.
VPValue *PlainCFGBuilder::getOrCreateVPOperand(Value *IRVal) {
  auto VPValIt = IRDef2VPValue.find(IRVal);
  if (VPValIt != IRDef2VPValue.end())
    // Operand has an associated VPInstruction or VPValue that was previously
    // created.
    return VPValIt->second;

  // Operand doesn't have a previously created VPInstruction/VPValue. This
  // means that operand is:
  //   A) a definition external to VPlan,
  //   B) any other Value without specific representation in VPlan.
  // For now, we use VPValue to represent A and B and classify both as external
  // definitions. We may introduce specific VPValue subclasses for them in the
  // future.
  assert(isExternalDef(IRVal) && "Expected external definition as operand.");

  // A and B: Create VPValue and add it to the pool of external definitions and
  // to the Value->VPValue map.
  VPValue *NewVPVal = Plan.getOrAddLiveIn(IRVal);
  IRDef2VPValue[IRVal] = NewVPVal;
  return NewVPVal;
}

// Create new VPInstructions in a VPBasicBlock, given its BasicBlock
// counterpart. This function must be invoked in RPO so that the operands of a
// VPInstruction in \p BB have been visited before (except for Phi nodes).
void PlainCFGBuilder::createVPInstructionsForVPBB(VPBasicBlock *VPBB,
                                                  BasicBlock *BB) {
  VPIRBuilder.setInsertPoint(VPBB);
  // TODO: Model and preserve debug intrinsics in VPlan.
  for (Instruction &InstRef : BB->instructionsWithoutDebug(false)) {
    Instruction *Inst = &InstRef;

    // There shouldn't be any VPValue for Inst at this point. Otherwise, we
    // visited Inst when we shouldn't, breaking the RPO traversal order.
    assert(!IRDef2VPValue.count(Inst) &&
           "Instruction shouldn't have been visited.");

    if (auto *Br = dyn_cast<BranchInst>(Inst)) {
      if (TheLoop->getLoopLatch() == BB ||
          any_of(successors(BB),
                 [this](BasicBlock *Succ) { return !TheLoop->contains(Succ); }))
        continue;

      // Conditional branch instruction are represented using BranchOnCond
      // recipes.
      if (Br->isConditional()) {
        VPValue *Cond = getOrCreateVPOperand(Br->getCondition());
        VPIRBuilder.createNaryOp(VPInstruction::BranchOnCond, {Cond}, Inst);
      }

      // Skip the rest of the Instruction processing for Branch instructions.
      continue;
    }

    if (auto *SI = dyn_cast<SwitchInst>(Inst)) {
      SmallVector<VPValue *> Ops = {getOrCreateVPOperand(SI->getCondition())};
      for (auto Case : SI->cases())
        Ops.push_back(getOrCreateVPOperand(Case.getCaseValue()));
      VPIRBuilder.createNaryOp(Instruction::Switch, Ops, Inst);
      continue;
    }

    VPSingleDefRecipe *NewR;
    if (auto *Phi = dyn_cast<PHINode>(Inst)) {
      // Phi node's operands may have not been visited at this point. We create
      // an empty VPInstruction that we will fix once the whole plain CFG has
      // been built.
      NewR = new VPWidenPHIRecipe(Phi, nullptr, Phi->getDebugLoc(), "vec.phi");
      VPBB->appendRecipe(NewR);
      if (isHeaderBB(Phi->getParent(), LI->getLoopFor(Phi->getParent()))) {
        // Header phis need to be fixed after the VPBB for the latch has been
        // created.
        PhisToFix.push_back(Phi);
      } else {
        // Add operands for VPPhi in the order matching its predecessors in
        // VPlan.
        DenseMap<const VPBasicBlock *, VPValue *> VPPredToIncomingValue;
        for (unsigned I = 0; I != Phi->getNumOperands(); ++I) {
          VPPredToIncomingValue[BB2VPBB[Phi->getIncomingBlock(I)]] =
              getOrCreateVPOperand(Phi->getIncomingValue(I));
        }
        for (VPBlockBase *Pred : VPBB->getPredecessors())
          NewR->addOperand(
              VPPredToIncomingValue.lookup(Pred->getExitingBasicBlock()));
      }
    } else {
      // Translate LLVM-IR operands into VPValue operands and set them in the
      // new VPInstruction.
      SmallVector<VPValue *, 4> VPOperands;
      for (Value *Op : Inst->operands())
        VPOperands.push_back(getOrCreateVPOperand(Op));

      // Build VPInstruction for any arbitrary Instruction without specific
      // representation in VPlan.
      NewR = cast<VPInstruction>(
          VPIRBuilder.createNaryOp(Inst->getOpcode(), VPOperands, Inst));
    }

    IRDef2VPValue[Inst] = NewR;
  }
}

// Main interface to build the plain CFG.
void PlainCFGBuilder::buildPlainCFG(
    DenseMap<VPBlockBase *, BasicBlock *> &VPB2IRBB) {

  // 1. Scan the body of the loop in a topological order to visit each basic
  // block after having visited its predecessor basic blocks. Create a VPBB for
  // each BB and link it to its successor and predecessor VPBBs. Note that
  // predecessors must be set in the same order as they are in the incomming IR.
  // Otherwise, there might be problems with existing phi nodes and algorithm
  // based on predecessors traversal.

  // Loop PH needs to be explicitly visited since it's not taken into account by
  // LoopBlocksDFS.
  BasicBlock *ThePreheaderBB = TheLoop->getLoopPreheader();
  assert((ThePreheaderBB->getTerminator()->getNumSuccessors() == 1) &&
         "Unexpected loop preheader");
  for (auto &I : *ThePreheaderBB) {
    if (I.getType()->isVoidTy())
      continue;
    IRDef2VPValue[&I] = Plan.getOrAddLiveIn(&I);
  }

  LoopBlocksRPO RPO(TheLoop);
  RPO.perform(LI);

  for (BasicBlock *BB : RPO) {
    // Create or retrieve the VPBasicBlock for this BB.
    VPBasicBlock *VPBB = getOrCreateVPBB(BB);
    VPRegionBlock *Region = VPBB->getParent();
    Loop *LoopForBB = LI->getLoopFor(BB);
    // Set VPBB predecessors in the same order as they are in the incoming BB.
    if (!isHeaderBB(BB, LoopForBB)) {
      setVPBBPredsFromBB(VPBB, BB);
    } else if (Region) {
      // BB is a loop header and there's a corresponding region, set the
      // predecessor for it.
      setRegionPredsFromBB(Region, BB);
    }

    // Create VPInstructions for BB.
    createVPInstructionsForVPBB(VPBB, BB);

    if (BB == TheLoop->getLoopLatch()) {
      VPBasicBlock *HeaderVPBB = getOrCreateVPBB(LoopForBB->getHeader());
      VPBlockUtils::connectBlocks(VPBB, HeaderVPBB);
      continue;
    }

    // Set VPBB successors. We create empty VPBBs for successors if they don't
    // exist already. Recipes will be created when the successor is visited
    // during the RPO traversal.
    if (auto *SI = dyn_cast<SwitchInst>(BB->getTerminator())) {
      SmallVector<VPBlockBase *> Succs = {
          getOrCreateVPBB(SI->getDefaultDest())};
      for (auto Case : SI->cases())
        Succs.push_back(getOrCreateVPBB(Case.getCaseSuccessor()));
      VPBB->setSuccessors(Succs);
      continue;
    }
    auto *BI = cast<BranchInst>(BB->getTerminator());
    unsigned NumSuccs = succ_size(BB);
    if (NumSuccs == 1) {
      auto *Successor = getOrCreateVPBB(BB->getSingleSuccessor());
      VPBB->setOneSuccessor(isHeaderVPBB(Successor)
                                ? Successor->getParent()
                                : static_cast<VPBlockBase *>(Successor));
      continue;
    }
    assert(BI->isConditional() && NumSuccs == 2 && BI->isConditional() &&
           "block must have conditional branch with 2 successors");

    BasicBlock *IRSucc0 = BI->getSuccessor(0);
    BasicBlock *IRSucc1 = BI->getSuccessor(1);
    VPBasicBlock *Successor0 = getOrCreateVPBB(IRSucc0);
    VPBasicBlock *Successor1 = getOrCreateVPBB(IRSucc1);
    if (BB == LoopForBB->getLoopLatch()) {
      // For a latch we need to set the successor of the region rather than that
      // of VPBB and it should be set to the exit, i.e., non-header successor,
      // except for the top region, which is handled elsewhere.
      assert(LoopForBB != TheLoop &&
             "Latch of the top region should have been handled earlier");
      Region->setOneSuccessor(isHeaderVPBB(Successor0) ? Successor1
                                                       : Successor0);
      Region->setExiting(VPBB);
      continue;
    }

    // Don't connect any blocks outside the current loop except the latch for
    // now. The latch is handled above.
    if (LoopForBB) {
      if (!LoopForBB->contains(IRSucc0)) {
        VPBB->setOneSuccessor(Successor1);
        continue;
      }
      if (!LoopForBB->contains(IRSucc1)) {
        VPBB->setOneSuccessor(Successor0);
        continue;
      }
    }

    VPBB->setTwoSuccessors(Successor0, Successor1);
  }

  // 2. The whole CFG has been built at this point so all the input Values must
  // have a VPlan counterpart. Fix VPlan header phi by adding their
  // corresponding VPlan operands.
  fixHeaderPhis();

  VPBlockUtils::connectBlocks(Plan.getEntry(),
                              getOrCreateVPBB(TheLoop->getHeader()));

  for (const auto &[IRBB, VPB] : BB2VPBB)
    VPB2IRBB[VPB] = IRBB;
}

void VPlanHCFGBuilder::buildPlainCFG() {
  PlainCFGBuilder PCFGBuilder(TheLoop, LI, Plan);
  PCFGBuilder.buildPlainCFG(VPB2IRBB);
}

// Public interface to build a H-CFG.
void VPlanHCFGBuilder::buildHierarchicalCFG() {
  // Build Top Region enclosing the plain CFG.
  buildPlainCFG();
  LLVM_DEBUG(Plan.setName("HCFGBuilder: Plain CFG\n"); dbgs() << Plan);
}
