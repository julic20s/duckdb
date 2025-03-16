#include "duckdb/execution/expression_ir_generator.hpp"
#include "duckdb/common/enums/expression_type.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"

namespace duckdb {

vector<llvm::Value *> ExpressionIRGenerator::Generate(JITRewriter &rewriter, const vector<llvm::Value *> &input) {
	this->rewriter = &rewriter;
	this->input = &input;
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
