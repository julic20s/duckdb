#include "duckdb/execution/expression_ir_generator.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"

namespace duckdb {

bool BoundConstantExpression::IsCompilable() const {
	return HasNativeType(return_type.InternalType());
}

llvm::Value *ExpressionIRGenerator::Generate(BoundConstantExpression &expr) {
	return CreateNativeValue(rewriter->builder, expr.value);
}

} // namespace duckdb
