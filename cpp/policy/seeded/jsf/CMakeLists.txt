# JSF AV seeds (ICS-037): one for each check .github/scripts/cpp_jsf.py and
# cmake/Jsf.cmake make, each written to break that rule. cpp/policy/check-policy.sh
# requires each to be rejected with the diagnostic named on its first line.
# Object libraries only, so compile_commands.json lists them for clang-tidy
# and clang-query; never built by default.
foreach(seed IN ITEMS
    av3_complexity av9_non_ascii av11_trigraph av13_wide_string av22_stdio av24_abort av33_include_quotes
    av41_long_line av42_two_statements av50_type_name av52_constant_case av57_access_order
    av58_parameters av59_braces av61_brace av99_namespaces av107_block_scope_function
    av126_block_comment av127_commented_code av133_intro av149_octal av151_magic_number
    av154_bit_field av157_side_effect av158_parentheses av160_assignment av163_unsigned
    av170_indirection av190_continue av191_break av192_final_else av200_for av201_counter
    av208_throw av209_basic_type)
  add_library(seed_${seed} OBJECT EXCLUDE_FROM_ALL ${seed}.cpp)
  target_link_libraries(seed_${seed} PRIVATE ics::common)
endforeach()

# Rejected by the JSF warnings in cmake/Jsf.cmake.
foreach(seed IN ITEMS av185_old_style_cast av202_float_equal)
  add_library(seed_${seed} OBJECT EXCLUDE_FROM_ALL ${seed}.cpp)
  target_link_libraries(seed_${seed} PRIVATE ics::common)
  ics_jsf_target(seed_${seed})
endforeach()
