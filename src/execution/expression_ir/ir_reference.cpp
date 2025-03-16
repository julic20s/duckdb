#include "duckdb/execution/expression_ir_generator.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"

namespace duckdb {

bool BoundReferenceExpression::IsCompilable() const {
	return HasNativeType(return_type.InternalType());
}

llvm::Value *ExpressionIRGenerator::Generate(BoundReferenceExpression &expr) {
	return (*input)[expr.index];
}

} // namespace duckdb
