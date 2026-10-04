#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

echo "Building..."
make -s

echo "Running demo/test_identifiers.bas"
OUT=$(./basika demo/test_identifiers.bas 2>&1)
printf "%s\n" "$OUT"

if echo "$OUT" | grep -q " 5 "; then
  echo "Smoke test PASS: found A_B output 5"
else
  echo "Smoke test FAIL: expected output line '5' not found"
  exit 2
fi

echo "Running tests/system/args_test.bas with arguments"
OUT=$(./basika tests/system/args_test.bas "hello" "world space" 2>&1)
EXP="ARGC: 3 
0: tests/system/args_test.bas
1: hello
2: world space
CMD: hello world space"

if [ "$OUT" = "$EXP" ]; then
  echo "Argument test PASS"
else
  echo "Argument test FAIL"
  printf "Expected:\n%s\nActual:\n%s\n" "$EXP" "$OUT"
  exit 2
fi

TESTS=(
  "tests/arrays/array_test"
  "tests/arrays/numeric_array"
  "tests/arrays/dim_edge_cases"
  "tests/arrays/multidim_bounds"
  "tests/arrays/erase"
  "tests/arrays/option_base_test"
  "tests/file_io/filo_block"
  "tests/file_io/filo_random"
  "tests/file_io/putget"
  "tests/file_io/put_from_array"
  "tests/file_io/get_into_array"
  "tests/file_io/delete"
  "tests/file_io/delete_line"
  "tests/file_io/name_test"
  "tests/file_io/kill_wildcard"
  "tests/file_io/files"
  "tests/file_io/dir_ops"
  "tests/file_io/dir_ops_ext"
  "tests/file_io/shell_test"
  "tests/audio/sound_play"
  "tests/audio/no_audio"
  "tests/audio/mml_coverage"
  "tests/audio/sound_range"
  "tests/audio/sound_frequency_low"
  "tests/audio/sound_frequency_high"
  "tests/audio/sound_duration_low"
  "tests/audio/sound_duration_high"
  "tests/audio/asc_chr_beep"
  "tests/strings/print_using"
  "tests/strings/print_using_ext"
  "tests/strings/tab_len"
  "tests/strings/string_funcs"
  "tests/strings/instr_val_str"
  "tests/strings/string_cmp"
  "tests/strings/more_string_funcs"
  "tests/strings/string_numeric_funcs"
  "tests/strings/compression_test"
  "tests/strings/long_string_test"
  "tests/strings/dynamic_string_growth"
  "tests/strings/dynamic_string_functions"
  "tests/strings/fixed_string_declarations"
  "tests/strings/basic_string_storage_test"
  "tests/file_io/mki_mks_mkd"
  "tests/strings/input_multi"
  "tests/strings/get_dollar"
  "tests/strings/inkey_dollar"
  "tests/errors/on_error_goto"
  "tests/strings/environ_test"
  "tests/math/math_funcs"
  "tests/math/def_fn"
  "tests/math/def_fn_compiled"
  "tests/math/peek_poke"
  "tests/math/random"
  "tests/math/data_read_restore"
  "tests/math/swap"
  "tests/math/swap_array"
  "tests/math/variable_suffixes"
  "tests/control_flow/nested_for"
  "tests/control_flow/nested_while"
  "tests/control_flow/on_goto_nested"
  "tests/control_flow/on_goto_gosub"
  "tests/control_flow/if_else_basic"
  "tests/control_flow/if_else_no_else"
  "tests/control_flow/if_else_multiple_statements"
  "tests/control_flow/if_else_nested"
  "tests/control_flow/if_else_string_vars"
  "tests/control_flow/block_if"
  "tests/control_flow/do_loop_infinite_exit"
  "tests/control_flow/do_while_loop"
  "tests/control_flow/do_while_loop_skip"
  "tests/control_flow/do_until_loop"
  "tests/control_flow/do_loop_while"
  "tests/control_flow/do_loop_while_runs_once"
  "tests/control_flow/do_loop_until"
  "tests/control_flow/do_loop_nested"
  "tests/control_flow/do_loop_nested_do"
  "tests/control_flow/do_loop_procedures"
  "tests/control_flow/select_case_values"
  "tests/control_flow/select_case_range"
  "tests/control_flow/select_case_is"
  "tests/control_flow/select_case_string"
  "tests/control_flow/select_case_nested"
  "tests/control_flow/select_case_procedure"
  "tests/control_flow/select_case_loop_exit"
  "tests/errors/syntax_error"
  "tests/errors/unnumbered_syntax_error"
  "tests/errors/return_error"
  "tests/file_io/lof_loc_test"
  "tests/file_io/files_redirect_test"
  "tests/system/shebang_test"
  "tests/math/bitwise"
  "tests/math/bitwise_control"
  "tests/file_io/field_record"
  "tests/control_flow/on_timer"
  "tests/control_flow/nested_paren"
  "tests/math/def_types"
  "tests/graphics/auto_screenshot"
  "tests/graphics/graphics_primitives"
  "tests/graphics/circle_alpha"
  "tests/graphics/drawing_alpha"
  "tests/graphics/autodisplay"
  "tests/control_flow/labels_no_lines"
  "tests/procedures/qbasic_sub"
  "tests/procedures/qbasic_function"
  "tests/procedures/qbasic_scope"
  "tests/procedures/qbasic_nested_calls"
  "tests/user_types/user_types"
  "tests/user_types/user_types_arrays"
  "tests/user_types/user_types_assignment"
  "tests/user_types/user_types_record_io"
  "tests/user_types/user_types_duplicate_field"
  "tests/user_types/user_types_duplicate_type"
  "tests/user_types/user_types_unknown_field"
  "tests/user_types/user_types_missing_end"
  "tests/user_types/user_types_recursive"
  "tests/user_types/user_types_malformed_field"
  "tests/user_types/user_types_type_mismatch"
)

for t in "${TESTS[@]}"; do
  echo "Running $t.bas"
  # Prepare deterministic fixtures for tests that rely on filesystem timestamps
  if [ "$t" = "tests/file_io/files" ]; then
    printf "test\n" > tests/f.tmp
    # Set a fixed timestamp: 2026-06-10 09:01:16
    touch -t 202606100901.16 tests/f.tmp
  fi

  if [ "$t" = "tests/graphics/auto_screenshot" ]; then
    rm -f tests/auto_out.png
    OUT=$(./basika --headless "$t.bas" 2>&1)
    if [ ! -f tests/auto_out.png ]; then
      echo "$t FAIL: screenshot file tests/auto_out.png not found"
      exit 2
    fi
    rm -f tests/auto_out.png
  elif [ "$t" = "tests/graphics/graphics_primitives" ]; then
    rm -f tests/graphics_primitives.png
    OUT=$(./basika --headless "$t.bas" 2>&1)
    if [ ! -f tests/graphics_primitives.png ]; then
      echo "$t FAIL: screenshot file tests/graphics_primitives.png not found"
      exit 2
    fi
    rm -f tests/graphics_primitives.png
  elif [ "$t" = "tests/graphics/autodisplay" ]; then
    rm -f tests/autodisplay.png
    OUT=$(./basika --headless "$t.bas" 2>&1)
    if [ ! -f tests/autodisplay.png ]; then
      echo "$t FAIL: screenshot file tests/autodisplay.png not found"
      exit 2
    fi
    rm -f tests/autodisplay.png
  elif [ "$t" = "tests/graphics/circle_alpha" ]; then
    rm -f tests/circle_alpha.png
    OUT=$(./basika --headless "$t.bas" 2>&1)
    if [ ! -f tests/circle_alpha.png ]; then
      echo "$t FAIL: screenshot file tests/circle_alpha.png not found"
      exit 2
    fi
    rm -f tests/circle_alpha.png
  elif [ "$t" = "tests/graphics/drawing_alpha" ]; then
    rm -f tests/drawing_alpha.png
    rm -f tests/drawing_alpha.result
    OUT=$(./basika --headless "$t.bas" 2>&1)
    if [ ! -f tests/drawing_alpha.png ]; then
      echo "$t FAIL: screenshot file tests/drawing_alpha.png not found"
      exit 2
    fi
    if [ ! -f tests/drawing_alpha.result ] || [ "$(tr -d '[:space:]' < tests/drawing_alpha.result)" != "1" ]; then
      echo "$t FAIL: alpha blending assertions failed"
      cat tests/drawing_alpha.result 2>/dev/null || true
      exit 2
    fi
    rm -f tests/drawing_alpha.png
    rm -f tests/drawing_alpha.result
  elif [ "$t" = "tests/audio/asc_chr_beep" ]; then
    OUT=$(./basika "$t.bas" 2>&1)
  elif [[ "$t" == tests/audio/* ]]; then
    OUT=$(./basika --no-audio "$t.bas" 2>&1)
  else
    OUT=$(./basika "$t.bas" 2>&1)
  fi

  EXP_FILE="$t.expected"
  if [ ! -f "$EXP_FILE" ]; then
    echo "Expected file missing: $EXP_FILE"
    exit 2
  fi
  EXP=$(cat "$EXP_FILE")
  if [ "$OUT" = "$EXP" ]; then
    echo "$t PASS"
  else
    echo "$t FAIL"
    echo "--- expected ---"
    printf "%s\n" "$EXP"
    echo "--- actual ---"
    printf "%s\n" "$OUT"
    exit 2
  fi
done

if [ "${BASIKA_SKIP_PERFORMANCE:-0}" = "1" ]; then
  echo "Skipping performance regression guard (BASIKA_SKIP_PERFORMANCE=1)"
else
  echo "Running performance regression guard"
  bash tests/scripts/performance_guard.sh
fi

echo "All tests PASS"

echo "Cleaning up temp files"
rm -rf test_dir_ext tests/filo_test.tmp tests/putget.tmp tests/putfrom.tmp tests/getinto.tmp tests/delete_test.tmp tests/kill_tmp1.tmp tests/kill_tmp2.tmp tests/input_multi.tmp
rm -f tests/field_record.tmp
rm -f tests/user_types_record.tmp

exit 0
