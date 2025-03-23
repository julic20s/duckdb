#include "duckdb/execution/expression_ir_generator.hpp"
#include "duckdb/planner/expression/bound_parameter_expression.hpp"
namespace duckdb {

bool BoundParameterExpression::IsCompilable() const {
	return HasNativeType(return_type.InternalType());
}

llvm::Value *ExpressionIRGenerator::Generate(BoundParameterExpression &expr) {
	return CreateNativeValue(b, expr.parameter_data->GetValue());
}

} // namespace duckdb
