#include "duckdb/execution/jit_rewriter.hpp"

#include "duckdb/common/enums/physical_operator_type.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/execution/operator/projection/physical_projection.hpp"
#include "duckdb/execution/physical_operator.hpp"

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>

namespace duckdb {

unique_ptr<IRGenerator> IRGenerator::Create(const PhysicalOperator &op) {
	switch (op.type) {
	case PhysicalOperatorType::PROJECTION:
		return make_uniq<ProjectionIRGenerator>();
	default:
		throw InternalException("Attempt to create IR generator for a unknown physical operator type!");
	}
}

JITRewriter::JITRewriter() {
	auto *engine_ptr = llvm::EngineBuilder().setEngineKind(llvm::EngineKind::JIT).create();
	engine.reset(engine_ptr);
}

JITRewriter::CompilingFunctionScope::CompilingFunctionScope(JITRewriter &rewriter, std::string name,
                                                            std::function<void(uint64_t)> callback)
    : rewriter(rewriter), name(std::move(name)), callback(std::move(callback)) {
	this->name = rewriter.GenerateTag(this->name);
	auto mod = make_uniq<llvm::Module>(name, rewriter.context);
	
	auto &b = rewriter.builder;
	auto executeFuncType = llvm::FunctionType::get(b.getVoidTy(), {b.getPtrTy(), b.getPtrTy()}, false);
	auto executeFunc = llvm::Function::Create(executeFuncType, llvm::Function::ExternalLinkage, this->name, *mod);
	auto executeBlock = llvm::BasicBlock::Create(rewriter.context, this->name + ".entry", executeFunc);
	b.SetInsertPoint(executeBlock);
}

JITRewriter::CompilingFunctionScope::~CompilingFunctionScope() {
	callback(rewriter.engine->getFunctionAddress(name));
}

} // namespace duckdb
