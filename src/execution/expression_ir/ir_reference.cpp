#include "duckdb/execution/expression_ir_generator.hpp"
#include "duckdb/ir/ir_value.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"

#include <llvm-19/llvm/ADT/Twine.h>
#include <llvm/IR/DerivedTypes.h>

namespace duckdb {

bool BoundReferenceExpression::IsCompilable() const {
	return HasNativeType(return_type.InternalType());
}

llvm::Value *ExpressionIRGenerator::Generate(BoundReferenceExpression &expr) {
	unsigned member = input_tuple_index->at(expr.index);
	auto name = "input." + llvm::Twine(expr.index);
	IRValue<void *> ptr(b.CreateStructGEP(input_type, input_ptr.v, member, name + ".ptr"));
	return b.CreateLoad(input_type->getStructElementType(member), ptr.v, name);
}

} // namespace duckdb
