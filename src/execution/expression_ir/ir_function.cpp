#include "duckdb/execution/expression_ir_generator.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"

#include <algorithm>

namespace duckdb {

bool BoundFunctionExpression::IsCompilable() const {
	if (function.ir_generate == nullptr) {
		return false;
	}
	return std::all_of(children.begin(), children.end(), [](auto &child) { return child->IsCompilable(); });
}

llvm::Value *ExpressionIRGenerator::Generate(BoundFunctionExpression &expr) {
	vector<llvm::Value *> arguments;
	arguments.reserve(expr.children.size());
	for (auto &child : expr.children) {
		arguments.push_back(Generate(*child));
	}
	return expr.function.ir_generate(b, arguments);
}

} // namespace duckdb
