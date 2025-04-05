#include "duckdb/execution/jit_engine.hpp"

#include "duckdb/common/file_system.hpp"
#include "duckdb/common/local_file_system.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/main/client_config.hpp"

#include <llvm-19/llvm/ADT/Twine.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/Transforms/IPO/GlobalOpt.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/NewGVN.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>
#include <llvm/Transforms/Vectorize/LoopVectorize.h>
#include <llvm/Transforms/Vectorize/SLPVectorizer.h>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace duckdb {

JITFunction::JITFunction(llvm::Module &mod, llvm::Twine name, llvm::FunctionType *type,
                         llvm::GlobalValue::LinkageTypes link) {
	fn = llvm::Function::Create(type, link, name, mod);
}

llvm::BasicBlock *JITFunction::CreateNewBlock(llvm::Twine name) {
	return llvm::BasicBlock::Create(fn->getContext(), name, fn);
}

JITFunction JITModule::CreateFunction(llvm::Twine name, llvm::FunctionType *type,
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

	auto machine_builder = llvm::orc::JITTargetMachineBuilder::detectHost();
	auto m = machine_builder->createTargetMachine();
	if (auto err = m.takeError()) {
		throw std::runtime_error(llvm::toString(std::move(err)));
	}

	machine = std::move(m.get());

	auto jit_ptr =
	    llvm::orc::LLJITBuilder()
	        .setObjectLinkingLayerCreator([](llvm::orc::ExecutionSession &ES, const llvm::Triple &) {
		        auto linking_layer = std::make_unique<llvm::orc::RTDyldObjectLinkingLayer>(
		            ES, [] { return std::make_unique<llvm::SectionMemoryManager>(); });
		        // Register the event listener.
		        linking_layer->registerJITEventListener(*llvm::JITEventListener::createGDBRegistrationListener());
		        // Make sure the debug info sections aren't stripped.
		        linking_layer->setProcessAllSections(true);
		        return linking_layer;
	        })
	        .create();
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

llvm::orc::ResourceTrackerSP JITEngine::RegisterModule(ClientContext &context, JITModule mod) {
	std::string_view name = mod.mod->getModuleIdentifier();

	bool ir_print = ClientConfig::GetConfig(context).query_compilation == QueryCompilationMode::WITH_LOGGING;

	if (ir_print) {
		PrintIRToFile("./" + string(name) + ".ll", mod.Get());
	}

	OptimizeIR(mod.Get(), llvm::OptimizationLevel::O2);

	if (ir_print) {
		PrintIRToFile("./" + string(name) + ".O2.ll", mod.Get());
	}

	auto res = lib->createResourceTracker();
	auto thread_safe_mod = llvm::orc::ThreadSafeModule(std::move(mod.mod), std::move(mod.ctx));
	if (auto err = jit->addIRModule(res, std::move(thread_safe_mod))) {
		throw std::runtime_error(llvm::toString(std::move(err)));
	}

	return mods[name] = std::move(res);
}

void JITEngine::UnregisterModule(std::string_view name) {
	mods.erase(name);
}

void JITEngine::OptimizeIR(llvm::Module &mod, llvm::OptimizationLevel level) {
	llvm::LoopAnalysisManager LAM;
	llvm::FunctionAnalysisManager FAM;
	FAM.registerPass([this] { return machine.get()->getTargetIRAnalysis(); });
	llvm::CGSCCAnalysisManager CGAM;
	llvm::ModuleAnalysisManager MAM;

	llvm::PassBuilder passes;
	passes.registerModuleAnalyses(MAM);
	passes.registerCGSCCAnalyses(CGAM);
	passes.registerFunctionAnalyses(FAM);
	passes.registerLoopAnalyses(LAM);
	passes.crossRegisterProxies(LAM, FAM, CGAM, MAM);
	passes.buildPerModuleDefaultPipeline(level).run(mod, MAM);
}

void JITEngine::PrintIRToFile(const string &filename, llvm::Module &mod) {
	LocalFileSystem fs;
	FileOpenFlags flag = FileOpenFlags::FILE_FLAGS_WRITE | FileOpenFlags::FILE_FLAGS_FILE_CREATE_NEW;
	auto handle = fs.OpenFile(filename, flag);
	if (!handle) {
		throw std::runtime_error("Failed to open file for printing llvm ir.");
	}

	std::string str;
	llvm::raw_string_ostream ss(str);
	mod.print(ss, nullptr);
	handle->Write(str.data(), str.size());
}

} // namespace duckdb
