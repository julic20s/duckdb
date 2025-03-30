#include "duckdb/execution/operator/projection/physical_projection.hpp"

#include "duckdb/common/helper.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/execution/expression_ir_generator.hpp"
#include "duckdb/execution/jit_engine.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/parallel/meta_pipeline.hpp"
#include "duckdb/parallel/thread_context.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"

#include <algorithm>
#include <cassert>
#include <llvm-19/llvm/IR/Constants.h>
#include <llvm-19/llvm/IR/DerivedTypes.h>
#include <llvm-19/llvm/IR/Module.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <memory>

namespace duckdb {

class ProjectionState : public OperatorState {
public:
	explicit ProjectionState(ExecutionContext &context, const vector<unique_ptr<Expression>> &expressions)
	    : executor(context.client, expressions) {
	}

	ExpressionExecutor executor;

public:
	void Finalize(const PhysicalOperator &op, ExecutionContext &context) override {
		context.thread.profiler.Flush(op);
	}
};

PhysicalProjection::PhysicalProjection(vector<LogicalType> types, vector<unique_ptr<Expression>> select_list,
                                       idx_t estimated_cardinality)
    : PhysicalOperator(PhysicalOperatorType::PROJECTION, std::move(types), estimated_cardinality),
      select_list(std::move(select_list)) {
}

OperatorResultType PhysicalProjection::Execute(ExecutionContext &context, DataChunk &input, DataChunk &chunk,
                                               GlobalOperatorState &gstate, OperatorState &state_p) const {
	auto &state = state_p.Cast<ProjectionState>();
	state.executor.Execute(input, chunk);

	// execute_fn(input.data.data(), chunk.data.data());

	return OperatorResultType::NEED_MORE_INPUT;
}

unique_ptr<OperatorState> PhysicalProjection::GetOperatorState(ExecutionContext &context) const {
	return make_uniq<ProjectionState>(context, select_list);
}

unique_ptr<PhysicalOperator>
PhysicalProjection::CreateJoinProjection(vector<LogicalType> proj_types, const vector<LogicalType> &lhs_types,
                                         const vector<LogicalType> &rhs_types, const vector<idx_t> &left_projection_map,
                                         const vector<idx_t> &right_projection_map, const idx_t estimated_cardinality) {

	vector<unique_ptr<Expression>> proj_selects;
	proj_selects.reserve(proj_types.size());

	if (left_projection_map.empty()) {
		for (storage_t i = 0; i < lhs_types.size(); ++i) {
			proj_selects.emplace_back(make_uniq<BoundReferenceExpression>(lhs_types[i], i));
		}
	} else {
		for (auto i : left_projection_map) {
			proj_selects.emplace_back(make_uniq<BoundReferenceExpression>(lhs_types[i], i));
		}
	}
	const auto left_cols = lhs_types.size();

	if (right_projection_map.empty()) {
		for (storage_t i = 0; i < rhs_types.size(); ++i) {
			proj_selects.emplace_back(make_uniq<BoundReferenceExpression>(rhs_types[i], left_cols + i));
		}

	} else {
		for (auto i : right_projection_map) {
			proj_selects.emplace_back(make_uniq<BoundReferenceExpression>(rhs_types[i], left_cols + i));
		}
	}

	return make_uniq<PhysicalProjection>(std::move(proj_types), std::move(proj_selects), estimated_cardinality);
}

InsertionOrderPreservingMap<string> PhysicalProjection::ParamsToString() const {
	InsertionOrderPreservingMap<string> result;
	string projections;
	for (idx_t i = 0; i < select_list.size(); i++) {
		if (i > 0) {
			projections += "\n";
		}
		auto &expr = select_list[i];
		projections += expr->GetName();
	}
	result["__projections__"] = projections;

#ifdef DUCKDB_ENABLE_LLVM
	if (enable_compilation && IsCompilable()) {
		JITEngine engine;
		auto fn = GetExecuteTupleFn(engine);
		std::string ir_text;
		llvm::raw_string_ostream stream(ir_text);
		fn->print(stream, nullptr);
		result["__ir__"] = std::move(ir_text);
	}
#endif

	SetEstimatedCardinality(result, estimated_cardinality);
	return result;
}

void PhysicalProjection::BuildPipelines(Pipeline &current, MetaPipeline &meta_pipeline) {
	op_state.reset();

	auto &state = meta_pipeline.GetState();
	if (children.size() != 1) {
		throw InternalException("Projection operator not supported in BuildPipelines");
	}

	state.AddPipelineOperator(current, *this);
	children[0]->BuildPipelines(current, meta_pipeline);
}

bool PhysicalProjection::IsCompilable() const {
	auto is_compilable = [](const std::unique_ptr<Expression> &expr) {
		return expr->IsCompilable();
	};
	return std::all_of(select_list.begin(), select_list.end(), is_compilable);
}

llvm::Function *PhysicalProjection::GetExecuteTupleFn(JITEngine &engine) const {
	static constexpr char symbol[] = "ExecuteTuple";

	if (auto fn = mod.Get().getFunction(symbol)) {
		return fn;
	}

	auto [input_type, input_tuple_index] = ExpressionInputTypeGenerator(mod.GetContext(), select_list).Generate();

	llvm::IRBuilder<> b(mod.GetContext());
	auto fn_type = llvm::FunctionType::get(b.getVoidTy(),
	                                       llvm::ArrayRef<llvm::Type *> {
	                                           b.getPtrTy(), // input tuple pointer
	                                           b.getPtrTy(), // output pointer tuple pointer
	                                       },
	                                       false);

	auto fn = mod.CreateFunction(symbol, fn_type, llvm::GlobalValue::InternalLinkage);
	auto entry = fn.CreateNewBlock("entry");
	b.SetInsertPoint(entry);

	ExpressionIRGenerator gen(b, select_list);

	auto input = fn.Get()->getArg(0); // struct Tuple *input
	vector<llvm::Value *> result = gen.Generate(input, input_tuple_index);

	// Store all result to output tuple
	llvm::Value *output_ptr_ptr = fn.Get()->getArg(1);                                        // void *output[]
	llvm::Value *output_ptr = b.CreateLoad(gen.GetOutputArrType(), output_ptr_ptr, "output"); // void *output
	auto offset = llvm::ConstantInt::get(b.getIntPtrTy(mod.Get().getDataLayout()), sizeof(void *));
	for (auto r : result) {
		b.CreateStore(r, output_ptr);                                // *output = r
		output_ptr = b.CreatePtrAdd(output_ptr, offset, "output++"); // output++
	}
	b.CreateRetVoid();

	return fn.Get();
}

llvm::Function *PhysicalProjection::GetExecuteFn(JITEngine &engine) const {
	// llvm::Module &m = mod.Get();
	return GetExecuteTupleFn(engine);
}

} // namespace duckdb
