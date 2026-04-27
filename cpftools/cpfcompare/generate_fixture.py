#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

FUNCTION_COUNT = 28
ARRAY_SIZE = 8


def stage_function(index: int) -> str:
    previous = max(index - 1, 0)
    fallback = max(index - 2, 0)
    seed_shift = index + 1
    limit_shift = (index % 5) + 3
    lines = [
        f"int stage_{index:02d}(int seed, int limit) {{",
        f"    int total = stage_{previous:02d}(seed + {seed_shift}, limit + {limit_shift});" if index > 0 else "    int total = seed + global_bias;",
        f"    int values[{ARRAY_SIZE}];",
        "    int index_value = 0;",
        f"    for (index_value = 0; index_value < {ARRAY_SIZE}; index_value = index_value + 1) {{",
        f"        values[index_value] = total + index_value + {seed_shift};",
        "        total = total + values[index_value];",
        "        if (total > limit && seed > 0) {",
        f"            total = total - stage_{previous:02d}(index_value + {seed_shift}, {limit_shift + 2});" if index > 0 else "            total = total - index_value;",
        "        } else {",
        f"            total = total + stage_{fallback:02d}(index_value + {limit_shift}, {limit_shift + 1});" if index > 1 else "            total = total + limit;",
        "        }",
        "    }",
        "    while (total > limit * 2) {",
        f"        total = total - ({limit_shift} + global_bias % 5);",
        "    }",
        "    return total;",
        "}",
        "",
    ]
    return "\n".join(lines)


def build_fixture() -> str:
    parts = [
        "int global_bias = 17;",
        "int global_values[32];",
        "",
    ]
    for index in range(FUNCTION_COUNT):
        parts.append(stage_function(index))

    parts.extend(
        [
            "int main() {",
            "    int accumulator = 0;",
            "    int index = 0;",
            "    while (index < 12) {",
            f"        global_values[index] = stage_{FUNCTION_COUNT - 1:02d}(index + global_bias, 12 + index);",
            "        accumulator = accumulator + global_values[index];",
            "        if (accumulator % 2 == 0) {",
            "            accumulator = accumulator + stage_10(index + 3, 9);",
            "        } else {",
            "            accumulator = accumulator - stage_05(index + 1, 7);",
            "        }",
            "        index = index + 1;",
            "    }",
            "    if (accumulator > 5000) {",
            "        accumulator = accumulator - global_bias;",
            "    } else {",
            "        accumulator = accumulator + 11;",
            "    }",
            "    return accumulator % 97;",
            "}",
            "",
        ]
    )
    return "\n".join(parts)


def main() -> None:
    output_path = Path(__file__).with_name("fixtures").joinpath("programs", "c_subset_fixture.c")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(build_fixture(), encoding="utf-8")


if __name__ == "__main__":
    main()

