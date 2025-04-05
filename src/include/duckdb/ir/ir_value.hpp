#pragma once

#include <llvm-19/llvm/IR/Value.h>
namespace duckdb {

template <class T = void>
struct IRValue {
	explicit IRValue(llvm::Value *v) noexcept : v(v) {
	}

	IRValue &operator=(llvm::Value *v) noexcept {
		this->v = v;
		return *this;
	}

	llvm::Value *v;
};

} // namespace duckdb
