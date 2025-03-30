#include "duckdb/execution/jit_engine.hpp"

#include "duckdb/common/helper.hpp"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <stdexcept>
#include <string_view>

namespace duckdb {

JITFunction::JITFunction(llvm::Module &mod, llvm::StringRef name, llvm::FunctionType *type,
                         llvm::GlobalValue::LinkageTypes link) {
	fn = llvm::Function::Create(type, link, name, mod);
}

llvm::BasicBlock *JITFunction::CreateNewBlock(llvm::Twine name) {
	return llvm::BasicBlock::Create(fn->getContext(), name, fn);
}

JITFunction JITModule::CreateFunction(llvm::StringRef name, llvm::FunctionType *type,
                                                  llvm::GlobalValue::LinkageTypes link) {
	return JITFunction(*mod, name, type, link);
}

static void InitializeGlobal() {
	struct Flag {};
	static auto flag = []() -> Flag {
		llvm::InitializeNativeTarget();
		llvm::InitializeNativeTargetAsmPrinter();
		return {};
	}();
	(void)flag;
}

JITEngine::JITEngine() {
	InitializeGlobal();

	auto jit_ptr = llvm::orc::LLJITBuilder().create();
	if (auto err = jit_ptr.takeError()) {
		throw std::runtime_error(llvm::toString(std::move(err)));
	}
	jit.reset(jit_ptr->release());

	auto lib_ref = jit->createJITDylib("DuckDBJITLib");
	if (auto err = lib_ref.takeError()) {
		throw std::runtime_error(llvm::toString(std::move(err)));
	}
	lib = &lib_ref.get();
}

void JITEngine::RegisterModule(JITModule mod) {
	std::string_view name = mod.mod->getModuleIdentifier();
	auto res = lib->createResourceTracker();
	auto thread_safe_mod = llvm::orc::ThreadSafeModule(std::move(mod.mod), std::move(mod.ctx));
	if (auto err = jit->addIRModule(res, std::move(thread_safe_mod))) {
		throw std::runtime_error(llvm::toString(std::move(err)));
	}

	mods[name] = std::move(res);
}

void JITEngine::UnregisterModule(std::string_view name) {
	mods.erase(name);
}

} // namespace duckdb
