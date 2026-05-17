#!/usr/bin/env bash
# K1520 Emulator - Utility Script
# Supports build, test, clean, and GUI launch workflows.

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

if [[ -d "$PROJECT_DIR/venv" ]]; then
	PYTHON_BIN="$PROJECT_DIR/venv/bin/python3"
else
	PYTHON_BIN="python3"
fi

usage() {
	cat <<'EOF'
Usage: ./quickstart.sh [OPTIONS]

Options:
  -h, --help             Show this help and exit
  --clean                Remove generated artifacts (build dirs, test binaries, Python bytecode/cache)
  --build                Configure and build all C++ targets
  --debug-build          Configure and build with debug symbols (-g) and LOG_LEVEL=5
  --log-level LEVEL      Set log level (0=OFF, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=TRACE). Default: 3
  --test                 Run C++ and Python tests and print a report

Behavior without options:
  Build if needed, then start the GUI.

Examples:
  ./quickstart.sh --build                    # Release build with LOG_LEVEL=3
  ./quickstart.sh --debug-build              # Debug build with LOG_LEVEL=5
  ./quickstart.sh --log-level 5 --build      # Release build with LOG_LEVEL=5
  ./quickstart.sh --clean --build --test     # Full clean, rebuild, test
EOF
}

clean_artifacts() {
	echo "[clean] Removing generated artifacts..."

	rm -rf "$PROJECT_DIR/build" "$PROJECT_DIR/build_k1520"

	find "$PROJECT_DIR" -type d \( -name "__pycache__" -o -name ".pytest_cache" \) -prune -exec rm -rf {} +
	find "$PROJECT_DIR" -type f \( -name "*.pyc" -o -name "*.pyo" \) -delete

	# Common temporary/test outputs
	rm -f "$PROJECT_DIR/test_c_api" "$PROJECT_DIR"/core.* "$PROJECT_DIR"/*.log

	echo "[clean] Done"
}

build_all() {
	local build_type="${1:-Release}"
	local log_level="${2:-3}"

	echo "[build] Configuring and building targets (BUILD_TYPE=$build_type, LOG_LEVEL=$log_level)..."
	cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$build_type" -DLOG_LEVEL="$log_level"
	cmake --build "$BUILD_DIR" -j"$(nproc)"
	echo "[build] Done"
}

ensure_built() {
	if [[ ! -f "$BUILD_DIR/libk1520core.so" ]]; then
		echo "[info] Build artifacts missing - running build"
		build_all
	fi
}

run_tests() {
	local cpp_total=0
	local cpp_failed=0
	local py_total=0
	local py_failed=0

	ensure_built
	export LD_LIBRARY_PATH="$BUILD_DIR:${LD_LIBRARY_PATH:-}"

	echo "[test] Running C++ tests (ctest)..."
	if [[ -f "$BUILD_DIR/CTestTestfile.cmake" ]]; then
		set +e
		ctest --test-dir "$BUILD_DIR" --output-on-failure
		local ctest_rc=$?
		set -e

		if [[ $ctest_rc -eq 0 ]]; then
			cpp_failed=0
		else
			cpp_failed=1
		fi

		# Count discovered tests from ctest listing.
		cpp_total=$(ctest --test-dir "$BUILD_DIR" -N 2>/dev/null | grep -E 'Total Tests:' | awk '{print $3}' || true)
		cpp_total=${cpp_total:-0}
	else
		echo "[test] No CTest metadata found in build directory"
	fi

	echo "[test] Running Python tests/scripts..."
	local py_scripts=(
		"$PROJECT_DIR/test_integration.py"
		"$PROJECT_DIR/test_boot.py"
		"$PROJECT_DIR/test_boot_detailed.py"
	)

	for script in "${py_scripts[@]}"; do
		if [[ -f "$script" ]]; then
			py_total=$((py_total + 1))
			set +e
			"$PYTHON_BIN" "$script"
			local rc=$?
			set -e
			if [[ $rc -ne 0 ]]; then
				py_failed=$((py_failed + 1))
			fi
		fi
	done

	echo ""
	echo "===== Test Report ====="
	echo "C++ tests:    total=${cpp_total} failed=${cpp_failed}"
	echo "Python tests: total=${py_total} failed=${py_failed}"
	echo "======================="

	if [[ $cpp_failed -ne 0 || $py_failed -ne 0 ]]; then
		return 1
	fi
}

start_gui() {
	ensure_built
	export LD_LIBRARY_PATH="$BUILD_DIR:${LD_LIBRARY_PATH:-}"

	echo "[run] Starting GUI..."
	exec "$PYTHON_BIN" "$PROJECT_DIR/app/main.py"
}

DO_CLEAN=0
DO_BUILD=0
DO_DEBUG_BUILD=0
DO_TEST=0
LOG_LEVEL=3

if [[ $# -eq 0 ]]; then
	start_gui
fi

while [[ $# -gt 0 ]]; do
	case "$1" in
		-h|--help)
			usage
			exit 0
			;;
		--clean)
			DO_CLEAN=1
			shift
			;;
		--build)
			DO_BUILD=1
			shift
			;;
		--debug-build)
			DO_DEBUG_BUILD=1
			LOG_LEVEL=5
			shift
			;;
		--log-level)
			shift
			if [[ $# -lt 1 ]]; then
				echo "--log-level requires an argument (0-5)"
				exit 2
			fi
			if [[ ! "$1" =~ ^[0-5]$ ]]; then
				echo "Log level must be 0-5, got: $1"
				exit 2
			fi
			LOG_LEVEL="$1"
			shift
			;;
		--test)
			DO_TEST=1
			shift
			;;
		*)
			echo "Unknown option: $1"
			echo "Use --help for usage"
			exit 2
			;;
	esac
done

if [[ $DO_CLEAN -eq 1 ]]; then
	clean_artifacts
fi

if [[ $DO_DEBUG_BUILD -eq 1 ]]; then
	build_all Debug "$LOG_LEVEL"
fi

if [[ $DO_BUILD -eq 1 ]]; then
	build_all Release "$LOG_LEVEL"
fi

if [[ $DO_TEST -eq 1 ]]; then
	run_tests
fi

# If options were provided and none implied run-mode, we just exit success.
exit 0
