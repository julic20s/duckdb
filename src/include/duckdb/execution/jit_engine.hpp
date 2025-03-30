#pragma once

#include "duckdb/common/helper.hpp"
#include "duckdb/common/unique_ptr.hpp"

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
#include <llvm/IR/Type.h>
#include <unordered_map>

namespace duckdb {

class DatabaseInstance;

class JITFunction {
public:
	JITFunction(llvm::Module &mod, llvm::StringRef name, llvm::FunctionType *type,
	            llvm::GlobalValue::LinkageTypes link);

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
	explicit JITModule(llvm::StringRef name)
	    : ctx(make_uniq<llvm::LLVMContext>()), mod(make_uniq<llvm::Module>(name, *ctx)) {
	}

	llvm::LLVMContext &GetContext() noexcept {
		return *ctx;
	}

	llvm::Module &Get() noexcept {
		return *mod;
	}

	JITFunction CreateFunction(llvm::StringRef name, llvm::FunctionType *type, llvm::GlobalValue::LinkageTypes link);

private:
	unique_ptr<llvm::LLVMContext> ctx;
	unique_ptr<llvm::Module> mod;
	friend class JITEngine;
};

class JITEngine {
public:
	static JITEngine &Get(DatabaseInstance &);

	JITEngine();

	void RegisterModule(JITModule mod);
	void UnregisterModule(std::string_view name);

private:
	unique_ptr<llvm::orc::LLJIT> jit;
	llvm::orc::JITDylib *lib;
	std::unordered_map<std::string_view, llvm::orc::ResourceTrackerSP> mods;
};

} // namespace duckdb
