#include "duckdb/execution/expression_ir_generator.hpp"

#include "duckdb/common/enums/expression_type.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/ir/ir_value.hpp"
#include "duckdb/parser/expression_util.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Type.h>
#include <stdexcept>

namespace duckdb {

pair<llvm::Type *, unordered_map<storage_t, unsigned>> ExpressionInputTypeGenerator::Generate() {
	for (auto &expr : expressions) {
		Generate(*expr);
	}
	if (members.empty()) {
		return {llvm::Type::getVoidTy(ctx), {}};
	}
	auto type = llvm::StructType::create(members);
	members.clear();
	types.clear();
	return {type, std::move(input_tuple_index)};
}

void ExpressionInputTypeGenerator::ReferenceInput(storage_t index, const LogicalType &type) {
	if (!HasNativeType(type.InternalType())) {
		throw std::runtime_error("Attempt to reference input in IR as a unsupported type: " + type.ToString());
	}
	auto [it, added] = input_tuple_index.emplace(index, members.size());
	if (added) {
		members.push_back(GetNativeType(ctx, type.InternalType()));
		types.push_back(type.id());
	} else if (types[it->second] != type.id()) {
		throw std::runtime_error("Attempt to reference input in IR with a conflicting type.");
	}
}

void ExpressionInputTypeGenerator::Generate(Expression &expr) {
	switch (expr.expression_class) {
	case ExpressionClass::BOUND_CONJUNCTION:
		return Generate(expr.Cast<BoundConjunctionExpression>());
	case ExpressionClass::BOUND_CONSTANT:
		return;
	case ExpressionClass::BOUND_FUNCTION:
		return Generate(expr.Cast<BoundFunctionExpression>());
	case ExpressionClass::BOUND_PARAMETER:
		return;
	case ExpressionClass::BOUND_REF:
		return Generate(expr.Cast<BoundReferenceExpression>());
	default:
		throw std::runtime_error("Attempt to generate input type for unsupported expression.");
	}
}

void ExpressionInputTypeGenerator::Generate(BoundConjunctionExpression &expr) {
	for (auto &child : expr.children) {
		Generate(*child);
	}
}

void ExpressionInputTypeGenerator::Generate(BoundFunctionExpression &expr) {
	for (auto &child : expr.children) {
		Generate(*child);
	}
}

void ExpressionInputTypeGenerator::Generate(BoundReferenceExpression &expr) {
	ReferenceInput(expr.index, expr.return_type);
}

vector<llvm::Value *> ExpressionIRGenerator::Generate(llvm::Type *input_type, IRValue<void *> input_ptr,
                                                      const unordered_map<storage_t, unsigned> &input_tuple_index) {
	this->input_type = input_type;
	this->input_ptr = input_ptr;
	this->input_tuple_index = &input_tuple_index;
	vector<llvm::Value *> res;
	res.reserve(expressions.size());
	for (auto &expr : expressions) {
		res.push_back(Generate(*expr));
	}

	return res;
}

llvm::Value *ExpressionIRGenerator::Generate(Expression &expr) {
	switch (expr.expression_class) {
	case ExpressionClass::BOUND_CONJUNCTION:
		return Generate(expr.Cast<BoundConjunctionExpression>());
	case ExpressionClass::BOUND_CONSTANT:
		return Generate(expr.Cast<BoundConstantExpression>());
	case ExpressionClass::BOUND_FUNCTION:
		return Generate(expr.Cast<BoundFunctionExpression>());
	case ExpressionClass::BOUND_PARAMETER:
		return Generate(expr.Cast<BoundParameterExpression>());
	case ExpressionClass::BOUND_REF:
		return Generate(expr.Cast<BoundReferenceExpression>());
	default:
		throw InternalException("Unknown expression to generate IR.");
	}
}

} // namespace duckdb
