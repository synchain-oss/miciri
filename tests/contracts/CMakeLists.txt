# SPDX-License-Identifier: GPL-3.0-or-later
#
# 契约测试：Result 的形状与色调映射、阈值之间的关系、桥契约的 JS 与 C++ 对拍；
# 另外确认阈值生成脚本会拒绝各种坏输入（每个反例各自对准一条报错）。

miciri_add_test(NAME contract_result SOURCES result_test.cpp)
miciri_add_test(NAME contract_thresholds SOURCES thresholds_test.cpp)
miciri_add_test(NAME contract_bridge_parity SOURCES bridge_parity_test.cpp)

# thresholds_<名字>：用 cmake -P 跑生成脚本。ok 要求成功；其余要求失败且报错里有 <期望的片段>。
function(miciri_thresholds_case name expect json)
  set(_in "${CMAKE_CURRENT_BINARY_DIR}/thr-${name}.json")
  file(WRITE "${_in}" "${json}")
  set(_args "-DINPUT=${_in}" "-DSCHEMA=${MICIRI_THRESHOLDS_SCHEMA}" "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/thr-${name}.h")
  set(_gen "${CMAKE_SOURCE_DIR}/tools/gen-thresholds.cmake")
  if(expect)
    add_test(NAME thresholds_${name}
      COMMAND "${CMAKE_COMMAND}" ${_args} "-DGENERATOR=${_gen}" "-DEXPECT=${expect}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/expect-reject.cmake")
  else()
    add_test(NAME thresholds_${name} COMMAND "${CMAKE_COMMAND}" ${_args} -P "${_gen}")
  endif()
  set_tests_properties(thresholds_${name} PROPERTIES TIMEOUT 60)
endfunction()

set(_e [=["k":{"value":1,"state":"synthetic","source":"s","date":"2026-11-02"}]=])
miciri_thresholds_case(accepts_minimal "" "{\"schema_version\":1,\"g\":{${_e}}}")
miciri_thresholds_case(rejects_bad_version "must be 1" "{\"schema_version\":2,\"g\":{${_e}}}")
miciri_thresholds_case(rejects_missing_field "missing field date"
  [=[{"schema_version":1,"g":{"k":{"value":1,"state":"synthetic","source":"s"}}}]=])
miciri_thresholds_case(rejects_unknown_field "unknown field extra"
  [=[{"schema_version":1,"g":{"k":{"value":1,"state":"synthetic","source":"s","date":"2026-11-02","extra":1}}}]=])
miciri_thresholds_case(rejects_bad_state "is not in the schema enum"
  [=[{"schema_version":1,"g":{"k":{"value":1,"state":"fuzzy","source":"s","date":null}}}]=])
miciri_thresholds_case(rejects_synthetic_without_date "synthetic entry needs a date"
  [=[{"schema_version":1,"g":{"k":{"value":1,"state":"synthetic","source":"s","date":null}}}]=])
miciri_thresholds_case(rejects_pending_with_date "pending entry must have date null"
  [=[{"schema_version":1,"g":{"k":{"value":1,"state":"pending","source":"s","date":"2026-11-02"}}}]=])
miciri_thresholds_case(rejects_null_when_synthetic "null value is only allowed while pending"
  [=[{"schema_version":1,"g":{"k":{"value":null,"state":"synthetic","source":"s","date":"2026-11-02"}}}]=])
miciri_thresholds_case(rejects_string_value "value must be a number"
  [=[{"schema_version":1,"g":{"k":{"value":"x","state":"pending","source":"s","date":null}}}]=])
miciri_thresholds_case(rejects_bad_key "bad key name"
  [=[{"schema_version":1,"g":{"BadKey":{"value":1,"state":"pending","source":"s","date":null}}}]=])
