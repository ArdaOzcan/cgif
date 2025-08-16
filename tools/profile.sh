#!/bin/bash
set -e

usage() {
  echo "Usage: $0 [-c] [-m] [-t] [--callgrind] [--massif] [--tracy]"
  echo "  -c, --callgrind    Run valgrind callgrind and generate graphs"
  echo "  -m, --massif       Run valgrind massif and launch massif-visualizer"
  echo "  -t, --tracy        Run the program normally and launch tracy-profiler"
  exit 1
}

if [ $# -eq 0 ]; then
  usage
fi

RUN_CALLGRIND=0
RUN_MASSIF=0
RUN_TRACY=0

while [[ $# -gt 0 ]]; do
  arg="$1"
  case "$arg" in
    --callgrind)
      RUN_CALLGRIND=1
      shift
      ;;
    --massif)
      RUN_MASSIF=1
      shift
      ;;
    --tracy)
      RUN_TRACY=1
      shift
      ;;
    -[cmt]*)
      # Parse combined short options, e.g. -mct or -mtc
      opts="${arg:1}"
      for (( i=0; i<${#opts}; i++ )); do
        case "${opts:$i:1}" in
          c) RUN_CALLGRIND=1 ;;
          m) RUN_MASSIF=1 ;;
          t) RUN_TRACY=1 ;;
          *)
            echo "Unknown option: -${opts:$i:1}"
            usage
            ;;
        esac
      done
      shift
      ;;
    *)
      echo "Unknown option: $arg"
      usage
      ;;
  esac
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS_DIR="$ROOT_DIR/tools"
OUT_DIR="$TOOLS_DIR/out"
BUILD_DIR="$ROOT_DIR/build"

mkdir -p "$OUT_DIR"

echo "Building project..."
make

PROGRAM=$(find "$BUILD_DIR" -type f -executable -name "main" | head -n 1)
if [[ -z "$PROGRAM" ]]; then
  echo "Executable not found!"
  exit 1
fi

CALLGRIND_OUT="$OUT_DIR/callgrind.out"
MASSIF_OUT="$OUT_DIR/massif.out"
DOT_FILE="$OUT_DIR/output.dot"
PNG_FILE="$OUT_DIR/output.png"

if [[ $RUN_CALLGRIND -eq 1 ]]; then
  echo "Running Callgrind..."
  valgrind --tool=callgrind --callgrind-out-file="$CALLGRIND_OUT" "$PROGRAM"

  echo "Generating callgrind graph..."
  gprof2dot -s -f callgrind "$CALLGRIND_OUT" -o "$DOT_FILE"
  dot -Tpng "$DOT_FILE" -o "$PNG_FILE"
  echo "Callgrind graph generated: $PNG_FILE"
fi

if [[ $RUN_MASSIF -eq 1 ]]; then
  echo "Running Massif..."
  valgrind --tool=massif --massif-out-file="$MASSIF_OUT" "$PROGRAM"
  echo "Massif output generated: $MASSIF_OUT"

  echo "Launching massif-visualizer..."
  massif-visualizer "$MASSIF_OUT" &
fi

if [[ $RUN_TRACY -eq 1 ]]; then
  echo "Running program normally and launching tracy-profiler..."
  "$ROOT_DIR/vendor/tracy/profiler/build/tracy-profiler" -a 127.0.0.1 &
  "$PROGRAM" &
  PROGRAM_PID=$!

  wait $PROGRAM_PID
fi

echo "Done."

