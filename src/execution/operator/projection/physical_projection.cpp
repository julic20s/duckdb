#include "duckdb/execution/operator/projection/physical_projection.hpp"

#include "duckdb/common/helper.hpp"
#include "duckdb/common/typedefs.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/execution/expression_ir_generator.hpp"
#include "duckdb/execution/jit_engine.hpp"
#include "duckdb/ir/for_statement.hpp"
#include "duckdb/ir/ir_value.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/parallel/meta_pipeline.hpp"
#include "duckdb/parallel/thread_context.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm-19/llvm/ADT/Twine.h>
#include <llvm-19/llvm/IR/Constants.h>
#include <llvm-19/llvm/IR/DerivedTypes.h>
#include <llvm-19/llvm/IR/Function.h>
#include <llvm-19/llvm/IR/Instructions.h>
#include <llvm-19/llvm/IR/Module.h>
#include <llvm-19/llvm/Support/raw_ostream.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <memory>
#include <tuple>

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
	if (execute_fn != nullptr) {
		chunk.SetCardinality(input);
		auto i = input.data.data(), o = chunk.data.data();
		auto n = chunk.size();
		execute_fn(i, o, n);
	} else {
		auto &state = state_p.Cast<ProjectionState>();
		state.executor.Execute(input, chunk);
	}

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

	if (execute_fn == nullptr && IsCompilable()) {
		auto &context = current.GetClientContext();
		auto &profiler = QueryProfiler::Get(context);
		profiler.StartPhase(MetricsType::EXECUTOR_QUERY_COMPILATION);
		execute_fn = GetExecuteFn(context, context.db->GetJITEngine());
		profiler.EndPhase();
	}

	state.AddPipelineOperator(current, *this);
	children[0]->BuildPipelines(current, meta_pipeline);
}

bool PhysicalProjection::IsCompilable() const {
	auto is_compilable = [](const std::unique_ptr<Expression> &expr) {
		return expr->IsCompilable();
	};
	return enable_compilation && std::all_of(select_list.begin(), select_list.end(), is_compilable);
}

llvm::Function *PhysicalProjection::GetExecuteTupleFn(JITEngine &engine, JITModule &mod, llvm::Type **input_type_ptr,
                                                      unordered_map<storage_t, unsigned> *input_tuple_index_ptr) const {
	static constexpr char symbol[] = "ExecuteTuple";

	auto [input_type, input_tuple_index] = ExpressionInputTypeGenerator(mod.GetContext(), select_list).Generate();

	auto fn_inst = mod.Get().getFunction(symbol);
	if (fn_inst != nullptr) {
		if (input_type_ptr) {
			*input_type_ptr = input_type;
		}

		if (input_tuple_index_ptr) {
			*input_tuple_index_ptr = std::move(input_tuple_index);
		}
		return fn_inst;
	}

	llvm::IRBuilder<> b(mod.GetContext());
	auto fn_type = llvm::FunctionType::get(b.getVoidTy(),
	                                       llvm::ArrayRef<llvm::Type *> {
	                                           b.getPtrTy(), // input tuple pointer
	                                           b.getPtrTy(), // output pointer tuple pointer void*[]
	                                           b.getIntPtrTy(mod.Get().getDataLayout()), // output index
	                                       },
	                                       false);

	auto fn = mod.CreateFunction(symbol, fn_type, llvm::GlobalValue::InternalLinkage);
	auto entry = fn.CreateNewBlock("entry");
	b.SetInsertPoint(entry);

	ExpressionIRGenerator gen(b, select_list);

	IRValue<void *> input_ptr(fn.Get()->getArg(0));
	vector<llvm::Value *> result = gen.Generate(input_type, input_ptr, input_tuple_index);

	// Store all result to output tuple
	IRValue<void *[]> output_ptr_ptr(fn.Get()->getArg(1));
	IRValue<size_t> output_offset(fn.Get()->getArg(2));
	for (size_t i = 0; i < result.size(); i++) {
		IRValue<uint32_t> index(b.getInt32(uint32_t(i)));
		IRValue<void **> output_ptr(
		    b.CreateInBoundsGEP(b.getPtrTy(), output_ptr_ptr.v, {index.v}, "output." + llvm::Twine(i)));
		IRValue<void *> output_start(b.CreateLoad(b.getPtrTy(), output_ptr.v, "output." + llvm::Twine(i) + ".start"));
		auto target =
		    b.CreateInBoundsGEP(result[i]->getType(), output_start.v, {output_offset.v}, "output." + llvm::Twine(i));
		b.CreateStore(result[i], target);
	}
	b.CreateRetVoid();

	if (input_type_ptr) {
		*input_type_ptr = input_type;
	}

	if (input_tuple_index_ptr) {
		*input_tuple_index_ptr = std::move(input_tuple_index);
	}

	return fn.Get();
}

PhysicalProjection::ExecuteFnType PhysicalProjection::GetExecuteFn(ClientContext &context, JITEngine &engine) const {
	if (execute_fn != nullptr) {
		return execute_fn;
	}
	const auto symbol = ("Execute" + engine.GetAutoIncrimentTag()).str();

	JITModule mod("projection" + engine.GetAutoIncrimentTag());

	llvm::Type *input_tuple_type;
	unordered_map<storage_t, unsigned> input_tuple_index;
	llvm::Function *tuple_fn = GetExecuteTupleFn(engine, mod, &input_tuple_type, &input_tuple_index);

	llvm::IRBuilder<> b(mod.GetContext());
	auto fn_type = llvm::FunctionType::get(b.getVoidTy(),
	                                       {
	                                           b.getPtrTy(),                             // Vector input[]
	                                           b.getPtrTy(),                             // Vector output[]
	                                           b.getIntPtrTy(mod.Get().getDataLayout()), // size_t count
	                                       },
	                                       false);

	auto fn = mod.CreateFunction(symbol, fn_type, llvm::GlobalValue::ExternalLinkage);
	auto entry = fn.CreateNewBlock("entry");
	b.SetInsertPoint(entry);

	IRValue<Vector[]> input_columns(fn.Get()->getArg(0));
	IRValue<Vector[]> output_columns(fn.Get()->getArg(1));
	IRValue<void *[]> output_column_ptrs(
	    b.CreateAlloca(b.getPtrTy(), b.getInt32(uint32_t(select_list.size())), "output.vector.all.data"));
	for (size_t i = 0; i < select_list.size(); i++) {
		auto name = "output.vector." + llvm::Twine(i);
		IRValue<Vector *> output_vector_ptr(
		    b.CreateInBoundsPtrAdd(output_columns.v, b.getInt64(sizeof(Vector) * i), name + ".ptr"));
		auto dst = b.CreateInBoundsGEP(b.getPtrTy(), output_column_ptrs.v, {b.getInt32(uint32_t(i))}, name + ".dst");
		b.CreateStore(FlatVector::GetDataIR(b, output_vector_ptr, name), dst);
	}

	auto input_tuple_ptr = b.CreateAlloca(input_tuple_type, b.getInt32(1), "input.tuple");
	// (type, column data, struct member pointer)
	vector<std::tuple<llvm::Type *, IRValue<data_ptr_t>, IRValue<void *>>> input_tuple_members;
	input_tuple_members.reserve(input_tuple_index.size());
	for (auto [index, member] : input_tuple_index) {
		auto name = "input.vector." + llvm::Twine(index);
		IRValue<Vector *> column_ptr(
		    b.CreateInBoundsPtrAdd(input_columns.v, b.getInt64(sizeof(Vector) * index), name + ".ptr"));
		IRValue<data_ptr_t> data_ptr(FlatVector::GetDataIR(b, column_ptr, name));
		IRValue<void *> member_ptr(
		    b.CreateStructGEP(input_tuple_type, input_tuple_ptr, member, "input.tuple." + llvm::Twine(member)));
		input_tuple_members.emplace_back(input_tuple_type->getStructElementType(member), data_ptr, member_ptr);
	}

	ForStatementIR for_stmt(b, fn);
	IRValue<size_t> n(fn.Get()->getArg(2));
	llvm::PHINode *i;
	for_stmt(
	    [&](ForStatementIR &self) {
		    i = b.CreatePHI(n.v->getType(), 2, "i");
		    i->addIncoming(llvm::ConstantInt::get(n.v->getType(), 0), entry);
	    },
	    [&](ForStatementIR &self) { return b.CreateICmpULT(i, n.v, "cond"); },
	    [&](ForStatementIR &self) {
		    for (auto &[type, data, member] : input_tuple_members) {
			    auto ptr = b.CreateInBoundsGEP(type, data.v, {i});
			    auto scalar = b.CreateLoad(type, ptr);
			    b.CreateStore(scalar, member.v);
		    }
		    b.CreateCall(tuple_fn, {input_tuple_ptr, output_column_ptrs.v, i}, "call.tuple");
		    auto inc = llvm::ConstantInt::get(n.v->getType(), 1);
		    IRValue<size_t> next_i(b.CreateAdd(i, inc, "i.next"));
		    i->addIncoming(next_i.v, for_stmt.GetBodyBlock());
	    });

	b.CreateRetVoid();

	auto lib = engine.RegisterModule(context, std::move(mod));

	auto addr = engine.Lookup(symbol);
	if (auto err = addr.takeError()) {
		throw std::runtime_error(llvm::toString(std::move(err)));
	}

	return addr->toPtr<ExecuteFnType>();
}

} // namespace duckdb
