#include "duckdb/common/assert.hpp"
#include "duckdb/execution/expression_ir_generator.hpp"
#include "duckdb/planner/expression/bound_conjunction_expression.hpp"

#include <algorithm>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Value.h>

namespace duckdb {

bool BoundConjunctionExpression::IsCompilable() const {
	return HasNativeType(return_type.InternalType()) &&
	       std::all_of(children.begin(), children.end(), [](const auto &child) { return child->IsCompilable(); });
}

llvm::Value *ExpressionIRGenerator::Generate(BoundConjunctionExpression &expr) {
	llvm::IRBuilder<> &b = rewriter->builder;

	llvm::Value *result_ptr = b.CreateAlloca(b.getInt1Ty());
	bool first = true;
	D_ASSERT(expr.children.size() > 0);
	for (auto &child : expr.children) {
		llvm::Value *cond = b.CreateICmpNE(Generate(*child), b.getInt1(false));
		if (first) {
			b.CreateStore(cond, result_ptr);
			first = false;
			continue;
		}

		llvm::Value *cur_result = b.CreateLoad(b.getInt1Ty(), result_ptr);
		llvm::Value *next_result;
		switch (expr.type) {
		case ExpressionType::CONJUNCTION_AND:
			next_result = b.CreateAnd(cur_result, cond);
			break;
		case ExpressionType::CONJUNCTION_OR:
			next_result = b.CreateOr(cur_result, cond);
			break;
		default:
			throw InternalException("Unknown conjunction type!");
		}
		b.CreateStore(next_result, result_ptr);
	}

	return b.CreateLoad(b.getInt1Ty(), result_ptr);
}

} // namespace duckdb
