#pragma once

#include <cstdint>

namespace duckdb {

enum class QueryCompilationMode : uint8_t {
	//! No query compilation
	OFF = 0,
	//! Compile the query using LLVM
	ON = 1,
	//! Compile the query using LLVM and output the llvm ir to a file
    //! This is useful for debugging the LLVM compilation process
	WITH_LOGGING = 2,
};

}
