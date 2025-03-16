//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/execution/jit_rewriter.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/vector.hpp"

#include <cstdint>
#include <functional>
#include <llvm-19/llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Value.h>

namespace duckdb {

class PhysicalOperator;
class JITRewriter;

class IRGenerator {
public:
	static unique_ptr<IRGenerator> Create(const PhysicalOperator &op);

	virtual ~IRGenerator() {
	}
	virtual void Produce(JITRewriter &rewriter) {
	}
	virtual void Consume(JITRewriter &rewriter, const vector<llvm::Value *> &value) {
	}

	IRGenerator *producer = nullptr;
	unique_ptr<IRGenerator> consumer;
};

class JITRewriter {
public:
	JITRewriter();

	std::string GenerateTag(std::string name) {
		name += std::to_string(tag_id++);
		return name;
	}

	class CompilingFunctionScope {
	public:
		explicit CompilingFunctionScope(JITRewriter &rewriter, std::string name,
		                                std::function<void(uint64_t)> callback);

		~CompilingFunctionScope();

	private:
		JITRewriter &rewriter;
		std::string name;
		std::function<void(uint64_t)> callback;
	};

	uint64_t tag_id = 0;
	llvm::LLVMContext context;
	unique_ptr<llvm::ExecutionEngine> engine;
	llvm::IRBuilder<> builder {context};
};

} // namespace duckdb
