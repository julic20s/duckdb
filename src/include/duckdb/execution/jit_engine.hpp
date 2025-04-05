#pragma once

#include "duckdb/common/helper.hpp"
#include "duckdb/common/pair.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/main/client_context.hpp"

#include <atomic>
#include <llvm-19/llvm/ADT/SmallVector.h>
#include <llvm-19/llvm/Passes/OptimizationLevel.h>
#include <llvm-19/llvm/Target/TargetMachine.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Type.h>
#include <memory>

namespace duckdb {

class DatabaseInstance;

class JITFunction {
public:
	JITFunction(llvm::Module &mod, llvm::Twine name, llvm::FunctionType *type, llvm::GlobalValue::LinkageTypes link);

	llvm::BasicBlock *CreateNewBlock(llvm::Twine name);

	llvm::Function *Get() noexcept {
		return fn;
	}

private:
	llvm::Function *fn;
	friend class JITModule;
};

class JITModule {
public:
	explicit JITModule(llvm::Twine name)
	    : ctx(make_uniq<llvm::LLVMContext>()), mod(make_uniq<llvm::Module>(name.str(), *ctx)) {
	}

	llvm::LLVMContext &GetContext() noexcept {
		return *ctx;
	}

	llvm::Module &Get() noexcept {
		return *mod;
	}

	JITFunction CreateFunction(llvm::Twine name, llvm::FunctionType *type, llvm::GlobalValue::LinkageTypes link);

private:
	unique_ptr<llvm::LLVMContext> ctx;
	unique_ptr<llvm::Module> mod;
	friend class JITEngine;
};

class JITEngine {
public:
	static JITEngine &Get(DatabaseInstance &);

	JITEngine();

	llvm::orc::ResourceTrackerSP RegisterModule(ClientContext &context, JITModule mod);
	void UnregisterModule(std::string_view name);

	llvm::orc::LLJIT &GetJIT() noexcept {
		return *jit;
	}

	auto Lookup(llvm::StringRef name) {
		return jit->lookup(*lib, name);
	}

	llvm::orc::JITDylib &GetLib() noexcept {
		return *lib;
	}

	llvm::Twine GetAutoIncrimentTag() {
		return "." + llvm::Twine(tag_.fetch_add(1, std::memory_order_relaxed));
	}

private:
	void OptimizeIR(llvm::Module &mod, llvm::OptimizationLevel);

	void PrintIRToFile(const string &filename, llvm::Module &mod);

	std::atomic_int64_t tag_ {0};
	unique_ptr<llvm::orc::LLJIT> jit;
	llvm::orc::JITDylib *lib;
	unordered_map<std::string_view, llvm::orc::ResourceTrackerSP> mods;
	std::unique_ptr<llvm::TargetMachine> machine;
};

} // namespace duckdb
