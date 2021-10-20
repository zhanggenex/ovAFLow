/*
  Copyright 2015 Google LLC All rights reserved.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at:

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

/*
   american fuzzy lop - LLVM-mode instrumentation pass
   ---------------------------------------------------

   Written by Laszlo Szekeres <lszekeres@google.com> and
              Michal Zalewski <lcamtuf@google.com>

   LLVM integration design comes from Laszlo Szekeres. C bits copied-and-pasted
   from afl-as.c are Michal's fault.

   This library is plugged into LLVM when invoking clang through afl-clang-fast.
   It tells the compiler to add code roughly equivalent to the bits discussed
   in ../afl-as.h.
*/

#define AFL_LLVM_PASS

#include "../config.h"
#include "../debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include <llvm/Support/raw_ostream.h>
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

namespace {

  class AFLCoverage : public ModulePass {

    public:

      static char ID;
      AFLCoverage() : ModulePass(ID) { }

      bool runOnModule(Module &M) override;

      // StringRef getPassName() const override {
      //  return "American Fuzzy Lop Instrumentation";
      // }

  };

}

std::vector<std::string> func1 = {
  "malloc"
};

std::vector<std::string> func2 = {
  "calloc", "realloc", "fgets",
  "fread", "fwrite"
};

std::vector<std::string> func3 = {
  "memcpy", "memmove", "memchr",
  "memset", "strncpy", "strncat",
  "strncmp", "strxfrm", "read",
  "strnchr"
};

unsigned int func_type(std::string func_name) {

  for (std::vector<std::string>::size_type i = 0; i < func1.size(); i++) {
    if (func_name.find(func1[i]) != func_name.npos)
	  return 1;
  }

  for (std::vector<std::string>::size_type i = 0; i < func2.size(); i++) {
    if (func_name.find(func2[i]) != func_name.npos)
	  return 2;
  }

  for (std::vector<std::string>::size_type i = 0; i < func3.size(); i++) {
    if (func_name.find(func3[i]) != func_name.npos)
	  return 3;
  }

  return 0;
}

char AFLCoverage::ID = 0;


bool AFLCoverage::runOnModule(Module &M) {

  LLVMContext &C = M.getContext();

  IntegerType *Int8Ty  = IntegerType::getInt8Ty(C);
  IntegerType *Int32Ty = IntegerType::getInt32Ty(C);

  /* Show a banner */

  char be_quiet = 0;

  if (isatty(2) && !getenv("AFL_QUIET")) {

    SAYF(cCYA "afl-llvm-pass " cBRI VERSION cRST " by <lszekeres@google.com>\n");

  } else be_quiet = 1;

  /* Decide instrumentation ratio */

  char* inst_ratio_str = getenv("AFL_INST_RATIO");
  unsigned int inst_ratio = 100;

  if (inst_ratio_str) {

    if (sscanf(inst_ratio_str, "%u", &inst_ratio) != 1 || !inst_ratio ||
        inst_ratio > 100)
      FATAL("Bad value of AFL_INST_RATIO (must be between 1 and 100)");

  }

  /* Get globals for the SHM region and the previous location. Note that
     __afl_prev_loc is thread-local. */

	//GlobalVariable::GlobalVariable	(	Module & 	M,
	//Type * 	Ty,
	//bool 	isConstant,
	//LinkageTypes 	Linkage,
	//Constant * 	Initializer,
	//const Twine & 	Name = "",
	//GlobalVariable * 	InsertBefore = nullptr,
	//ThreadLocalMode 	TLMode = NotThreadLocal,
	//unsigned 	AddressSpace = 0,
	//bool 	isExternallyInitialized = false 
	//)

  GlobalVariable *AFLMapPtr =
      new GlobalVariable(M, PointerType::get(Int8Ty, 0), false,
                         GlobalValue::ExternalLinkage, 0, "__afl_area_ptr");

  GlobalVariable *AFLMapPtr_He =
      new GlobalVariable(M, PointerType::get(Int32Ty, 0), false,
                         GlobalValue::ExternalLinkage, 0, "__afl_area_ptr_he");

  GlobalVariable *AFLPrevLoc = new GlobalVariable(
      M, Int32Ty, false, GlobalValue::ExternalLinkage, 0, "__afl_prev_loc",
      0, GlobalVariable::GeneralDynamicTLSModel, 0, false);

  /* Instrument all the things! */

  int inst_blocks = 0;
  int inst_new = 0;

	for (auto &F : M)
    	for (auto &BB : F) {	

      BasicBlock::iterator IP = BB.getFirstInsertionPt();
      IRBuilder<> IRB(&(*IP));

      if (AFL_R(100) >= inst_ratio) continue;

      /* Make up cur_loc */

      unsigned int cur_loc = AFL_R(MAP_SIZE);

      ConstantInt *CurLoc = ConstantInt::get(Int32Ty, cur_loc);

      /* Load prev_loc */

      LoadInst *PrevLoc = IRB.CreateLoad(AFLPrevLoc);
      PrevLoc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
      Value *PrevLocCasted = IRB.CreateZExt(PrevLoc, IRB.getInt32Ty());

      /* Load SHM pointer */

      LoadInst *MapPtr = IRB.CreateLoad(AFLMapPtr);
      MapPtr->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
      Value *MapPtrIdx =
          IRB.CreateGEP(MapPtr, IRB.CreateXor(PrevLocCasted, CurLoc));

      /* Update bitmap */

      LoadInst *Counter = IRB.CreateLoad(MapPtrIdx);
      Counter->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
      Value *Incr = IRB.CreateAdd(Counter, ConstantInt::get(Int8Ty, 1));
      IRB.CreateStore(Incr, MapPtrIdx)
          ->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
      //update bitmap_he
      for (auto &INST : BB) {
        if (CallInst *CI = dyn_cast<CallInst>(&INST)) {
          if (Function *F = CI->getCalledFunction()) {
		    
			std::string func_name = F->getName().str();

			// check whether a function contains a pointer and an int (size) in the arguments

			bool hasPtr = false, hasSize = false;

			for (unsigned i = 0; i < CI->getNumArgOperands(); i++) {

			  if (Type* var_type = CI->getArgOperand(i)->getType()) {
				if (var_type->isPointerTy())
			      hasPtr = true;
				
				if (var_type->isIntegerTy())
			      hasSize = true;
			  }

			}

			if (hasPtr && hasSize) {
			  llvm::errs() << "---------------------" << func_name << "\n";
			}

			unsigned int type_tmp = func_type(func_name);
            if (type_tmp && type_tmp < CI->getNumArgOperands() + 1) {

              /* Load SHM_HE pointer */
			  //llvm::errs() << func_name << " " << type_tmp << "\n";

			  IRBuilder<> funcIRB(CI);

      		  unsigned int cur_loc_he = AFL_R(MAP_SIZE_HE);

      		  ConstantInt *CurLocHe = ConstantInt::get(Int32Ty, cur_loc_he);

              LoadInst *MapPtr_He = funcIRB.CreateLoad(AFLMapPtr_He);
              MapPtr_He->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
              Value *MapPtrIdx_He =
                      funcIRB.CreateGEP(MapPtr_He, funcIRB.CreateAnd(CurLocHe, CurLocHe));

              /* Update bitmap */

              //LoadInst *Counter_He = IRB.CreateLoad(MapPtrIdx_He);
              //Counter_He->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
			  //llvm::errs() << *CI->getArgOperand(type_tmp - 1) << "\n";
              Value *Incr_He = funcIRB.CreateAdd(CI->getArgOperand(type_tmp - 1), ConstantInt::get(Int32Ty, 0));
              funcIRB.CreateStore(Incr_He, MapPtrIdx_He)
                      ->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

			  inst_new++;
            }
          }
        }
      }

      /* Set prev_loc to cur_loc >> 1 */

      StoreInst *Store =
          IRB.CreateStore(ConstantInt::get(Int32Ty, cur_loc >> 1), AFLPrevLoc);
      Store->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

      inst_blocks++;

    }

  /* Say something nice. */

  if (!be_quiet) {

    if (!inst_blocks) WARNF("No instrumentation targets found.");
    else OKF("Instrumented %u locations (%s mode, ratio %u%%).",
             inst_blocks, getenv("AFL_HARDEN") ? "hardened" :
             ((getenv("AFL_USE_ASAN") || getenv("AFL_USE_MSAN")) ?
              "ASAN/MSAN" : "non-hardened"), inst_ratio);

    OKF("Instrumented %u new ", inst_new);
  }

  return true;

}


static void registerAFLPass(const PassManagerBuilder &,
                            legacy::PassManagerBase &PM) {

  PM.add(new AFLCoverage());

}


static RegisterStandardPasses RegisterAFLPass(
    PassManagerBuilder::EP_ModuleOptimizerEarly, registerAFLPass);

static RegisterStandardPasses RegisterAFLPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0, registerAFLPass);
