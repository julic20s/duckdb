add_library_unity(
  duckdb_expression_ir
  OBJECT
  ir_conjunction.cpp
  ir_constant.cpp
  ir_function.cpp
  ir_parameter.cpp
  ir_reference.cpp)
set(ALL_OBJECT_FILES
    ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:duckdb_expression_ir>
    PARENT_SCOPE)
