//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/execution/operator/projection/physical_projection.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/execution/jit_engine.hpp"
#include "duckdb/execution/physical_operator.hpp"
#include "duckdb/planner/expression.hpp"

#include <cstddef>
#include <llvm-19/llvm/IR/Module.h>

#ifdef DUCKDB_ENABLE_LLVM
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#endif

namespace duckdb {

class PhysicalProjection : public PhysicalOperator {
public:
	static constexpr const PhysicalOperatorType TYPE = PhysicalOperatorType::PROJECTION;

public:
	PhysicalProjection(vector<LogicalType> types, vector<unique_ptr<Expression>> select_list,
	                   idx_t estimated_cardinality);

	vector<unique_ptr<Expression>> select_list;

#ifdef DUCKDB_ENABLE_LLVM
	//! Enable compilation
	bool enable_compilation = false;
#endif

public:
	unique_ptr<OperatorState> GetOperatorState(ExecutionContext &context) const override;
	OperatorResultType Execute(ExecutionContext &context, DataChunk &input, DataChunk &chunk,
	                           GlobalOperatorState &gstate, OperatorState &state) const override;

	bool ParallelOperator() const override {
		return true;
	}

	InsertionOrderPreservingMap<string> ParamsToString() const override;

	void BuildPipelines(Pipeline &current, MetaPipeline &meta_pipeline) override;

	static unique_ptr<PhysicalOperator>
	CreateJoinProjection(vector<LogicalType> proj_types, const vector<LogicalType> &lhs_types,
	                     const vector<LogicalType> &rhs_types, const vector<idx_t> &left_projection_map,
	                     const vector<idx_t> &right_projection_map, const idx_t estimated_cardinality);

private:
#ifdef DUCKDB_ENABLE_LLVM
	bool IsCompilable() const;

	using ExecuteFnType = void (*)(Vector input[], Vector chunk[], size_t n);

	ExecuteFnType GetExecuteFn(ClientContext &context, JITEngine &engine) const;
	llvm::Function *GetExecuteTupleFn(JITEngine &engine, JITModule &mod, llvm::Type **input_type_ptr = nullptr,
	                                  unordered_map<storage_t, unsigned> *input_tuple_index_ptr = nullptr) const;

	ExecuteFnType execute_fn = nullptr;
#endif
};

} // namespace duckdb
