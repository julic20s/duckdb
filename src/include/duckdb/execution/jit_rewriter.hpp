//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/execution/jit_rewriter.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include <llvm-19/llvm/ADT/ArrayRef.h>
#include <llvm-19/llvm/IR/Function.h>
#include <llvm-19/llvm/IR/Module.h>
#include <llvm-19/llvm/IR/Type.h>
#include <memory>
#include <unordered_map>
#ifdef DUCKDB_ENABLE_LLVM

#include "duckdb/common/unique_ptr.hpp"

#include <cstdint>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Value.h>

namespace duckdb {

class DatabaseInstance;

class JITRewriter {
public:
	static JITRewriter &Get(DatabaseInstance &db);

	JITRewriter();

	JITRewriter(const JITRewriter &) = delete;

	llvm::LLVMContext context;

	std::string GenerateTag(std::string name) {
		name += std::to_string(tag_id++);
		return name;
	}

	class Function {
	public:
		Function(JITRewriter &rewriter, std::string name, llvm::Type *return_type,
		         llvm::ArrayRef<llvm::Type *> argument_types);

		void CreateNewBlock(llvm::IRBuilder<> &b, std::string_view name);

	private:
		std::string mangling_name;
		std::unique_ptr<llvm::Module> mod;
		llvm::Function *fn;

		friend class JITRewriter;
	};

	uint64_t Register(std::unique_ptr<Function> fn);

	void Unregister(uint64_t addr);

private:
	uint64_t tag_id = 0;
	unique_ptr<llvm::ExecutionEngine> engine;
	std::unordered_map<uint64_t, std::unique_ptr<Function>> registered;
};

} // namespace duckdb

#endif
