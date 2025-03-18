#include "duckdb/execution/operator/projection/physical_projection.hpp"

#include "duckdb/common/helper.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/execution/expression_ir_generator.hpp"
#include "duckdb/execution/jit_rewriter.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/parallel/meta_pipeline.hpp"
#include "duckdb/parallel/thread_context.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"

#include <cassert>
#include <llvm-19/llvm/ADT/ArrayRef.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>

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
	// auto &state = state_p.Cast<ProjectionState>();
	// state.executor.Execute(input, chunk);

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
	SetEstimatedCardinality(result, estimated_cardinality);
	return result;
}

void PhysicalProjection::BuildPipelines(Pipeline &current, MetaPipeline &meta_pipeline) {
	op_state.reset();

	auto &state = meta_pipeline.GetState();
	if (children.size() != 1) {
		throw InternalException("Projection operator not supported in BuildPipelines");
	}

	JITRewriter &rewriter = current.GetClientContext().db->GetJITRewriter();
	llvm::IRBuilder<> b(rewriter.context);
	auto fn = make_uniq<JITRewriter::Function>(rewriter, "projection_execute", b.getVoidTy(),
	                                           llvm::ArrayRef {
	                                               b.getPtrTy(), // void * input[]
	                                               b.getPtrTy(), // void * chunk[]
	                                           });
	fn->CreateNewBlock(b, "entry");
	ExpressionIRGenerator(select_list).Generate(rewriter, b, {});
	execute_fn = reinterpret_cast<decltype(execute_fn)>(rewriter.Register(std::move(fn)));

	state.AddPipelineOperator(current, *this);
	children[0]->BuildPipelines(current, meta_pipeline);
}

} // namespace duckdb
