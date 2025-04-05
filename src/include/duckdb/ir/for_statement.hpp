#pragma once

#include "duckdb/execution/jit_engine.hpp"

#include <llvm-19/llvm/IR/BasicBlock.h>
#include <llvm/IR/IRBuilder.h>

namespace duckdb {

class ForStatementIR {
public:
	explicit ForStatementIR(llvm::IRBuilder<> &b, JITFunction &fn) : b(b) {
		cond_block = fn.CreateNewBlock("for.cond");
		body_block = fn.CreateNewBlock("for.loop");
		exit_block = fn.CreateNewBlock("for.exit");
	}

	template <class Init, class Cond, class Body>
	void operator()(Init init, Cond cond, Body body) {
		b.CreateBr(cond_block);
		b.SetInsertPoint(cond_block);
		init(*this);
		b.CreateCondBr(cond(*this), body_block, exit_block);
		b.SetInsertPoint(body_block);
		body(*this);
		b.CreateBr(cond_block);
		b.SetInsertPoint(exit_block);
	}

	llvm::BasicBlock *GetCondBlock() noexcept {
		return cond_block;
	}

	llvm::BasicBlock *GetBodyBlock() noexcept {
		return body_block;
	}

	llvm::BasicBlock *GetExitBlock() noexcept {
		return exit_block;
	}

private:
	llvm::IRBuilder<> &b;

	llvm::BasicBlock *cond_block;
	llvm::BasicBlock *body_block;
	llvm::BasicBlock *exit_block;
};

} // namespace duckdb
