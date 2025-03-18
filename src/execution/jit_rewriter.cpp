#include "duckdb/execution/jit_rewriter.hpp"

#include "duckdb/common/helper.hpp"
#include "duckdb/common/unique_ptr.hpp"

#include <cstdint>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

namespace duckdb {

JITRewriter::JITRewriter() {
	auto *engine_ptr = llvm::EngineBuilder().setEngineKind(llvm::EngineKind::JIT).create();
	engine.reset(engine_ptr);
}

JITRewriter::Function::Function(JITRewriter &rewriter, std::string name, llvm::Type *return_type,
                                llvm::ArrayRef<llvm::Type *> argument_types) {
	mangling_name = rewriter.GenerateTag(std::move(name));
	mod = make_uniq<llvm::Module>(mangling_name, rewriter.context);

	auto fnType = llvm::FunctionType::get(return_type, argument_types, false);
	fn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, name, *mod);
}

void JITRewriter::Function::CreateNewBlock(llvm::IRBuilder<> &b, std::string_view name) {
	auto tag = mangling_name + ".";
	tag.append(name);
	auto executeBlock = llvm::BasicBlock::Create(b.getContext(), tag, fn);
	b.SetInsertPoint(executeBlock);
}

uint64_t JITRewriter::Register(std::unique_ptr<Function> fn) {
	uint64_t addr = engine->getFunctionAddress(fn->mangling_name);
	registered[addr] = std::move(fn);
	return addr;
}

void JITRewriter::Unregister(uint64_t addr) {
	registered.erase(addr);
}

} // namespace duckdb
