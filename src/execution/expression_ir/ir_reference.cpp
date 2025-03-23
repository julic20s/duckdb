#include "duckdb/execution/expression_ir_generator.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"

#include <llvm/IR/DerivedTypes.h>

namespace duckdb {

bool BoundReferenceExpression::IsCompilable() const {
	return HasNativeType(return_type.InternalType());
}

llvm::Value *ExpressionIRGenerator::Generate(BoundReferenceExpression &expr) {
	return b.CreateStructGEP(input->getType(), input, ref_map->at(expr.index));
}

} // namespace duckdb
