//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/execution/expression_ir_generator.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/types.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/execution/jit_rewriter.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/planner/expression/bound_conjunction_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/planner/expression/bound_parameter_expression.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>

namespace duckdb {

inline bool HasNativeType(PhysicalType type) noexcept {
	switch (type) {
	case PhysicalType::BOOL:
	case PhysicalType::UINT8:
	case PhysicalType::INT8:
	case PhysicalType::UINT16:
	case PhysicalType::INT16:
	case PhysicalType::UINT32:
	case PhysicalType::INT32:
	case PhysicalType::UINT64:
	case PhysicalType::INT64:
	case PhysicalType::FLOAT:
	case PhysicalType::DOUBLE:
		return true;
	default:
		return false;
	}
}

inline llvm::Type *GetNativeType(llvm::LLVMContext &c, PhysicalType type) noexcept {
	switch (type) {
	case PhysicalType::BOOL:
		return llvm::Type::getInt1Ty(c);
	case PhysicalType::UINT8:
	case PhysicalType::INT8:
		return llvm::Type::getInt8Ty(c);
	case PhysicalType::UINT16:
	case PhysicalType::INT16:
		return llvm::Type::getInt16Ty(c);
	case PhysicalType::UINT32:
	case PhysicalType::INT32:
		return llvm::Type::getInt32Ty(c);
	case PhysicalType::UINT64:
	case PhysicalType::INT64:
		return llvm::Type::getInt64Ty(c);
	case PhysicalType::FLOAT:
		return llvm::Type::getFloatTy(c);
	case PhysicalType::DOUBLE:
		return llvm::Type::getDoubleTy(c);
	default:
		return nullptr;
	}
}

inline llvm::Value *CreateNativeValue(llvm::IRBuilder<> &b, const Value &value) {
	switch (value.type().InternalType()) {
	case PhysicalType::BOOL:
		return b.getInt1(value.GetValueUnsafe<bool>());
	case PhysicalType::UINT8:
		return b.getInt8(value.GetValueUnsafe<uint8_t>());
	case PhysicalType::INT8:
		return b.getInt8(uint8_t(value.GetValueUnsafe<int8_t>()));
	case PhysicalType::UINT16:
		return b.getInt16(value.GetValueUnsafe<uint16_t>());
	case PhysicalType::INT16:
		return b.getInt16(uint16_t(value.GetValueUnsafe<int16_t>()));
	case PhysicalType::UINT32:
		return b.getInt32(value.GetValueUnsafe<uint32_t>());
	case PhysicalType::INT32:
		return b.getInt32(uint32_t(value.GetValueUnsafe<int32_t>()));
	case PhysicalType::UINT64:
		return b.getInt64(value.GetValueUnsafe<uint64_t>());
	case PhysicalType::INT64:
		return b.getInt64(uint64_t(value.GetValueUnsafe<int64_t>()));
	case PhysicalType::FLOAT:
		return llvm::ConstantFP::get(b.getFloatTy(), value.GetValueUnsafe<float>());
	case PhysicalType::DOUBLE:
		return llvm::ConstantFP::get(b.getDoubleTy(), value.GetValueUnsafe<double>());
	default:
		throw InternalException("BoundConstantExpressionIRGenerator::Consume for unknown type.");
	}
}

class ExpressionIRGenerator {
public:
	explicit ExpressionIRGenerator(const vector<unique_ptr<Expression>> &expressions) : expressions(expressions) {
	}

	vector<llvm::Value *> Generate(JITRewriter &rewriter, llvm::IRBuilder<> &b, const vector<llvm::Value *> &input);

private:
	llvm::Value *Generate(Expression &expr);
	llvm::Value *Generate(BoundConjunctionExpression &expr);
	llvm::Value *Generate(BoundConstantExpression &expr);
	llvm::Value *Generate(BoundFunctionExpression &expr);
	llvm::Value *Generate(BoundParameterExpression &expr);
	llvm::Value *Generate(BoundReferenceExpression &expr);

	const vector<unique_ptr<Expression>> &expressions;
	JITRewriter *rewriter;
	llvm::IRBuilder<> *b;
	const vector<llvm::Value *> *input;
};

} // namespace duckdb
