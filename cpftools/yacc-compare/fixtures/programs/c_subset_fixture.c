int global_bias = 17;
int global_values[32];

int stage_00(int seed, int limit) {
    int total = seed + global_bias;
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 1;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - index_value;
        } else {
            total = total + limit;
        }
    }
    while (total > limit * 2) {
        total = total - (3 + global_bias % 5);
    }
    return total;
}

int stage_01(int seed, int limit) {
    int total = stage_00(seed + 2, limit + 4);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 2;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_00(index_value + 2, 6);
        } else {
            total = total + limit;
        }
    }
    while (total > limit * 2) {
        total = total - (4 + global_bias % 5);
    }
    return total;
}

int stage_02(int seed, int limit) {
    int total = stage_01(seed + 3, limit + 5);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 3;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_01(index_value + 3, 7);
        } else {
            total = total + stage_00(index_value + 5, 6);
        }
    }
    while (total > limit * 2) {
        total = total - (5 + global_bias % 5);
    }
    return total;
}

int stage_03(int seed, int limit) {
    int total = stage_02(seed + 4, limit + 6);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 4;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_02(index_value + 4, 8);
        } else {
            total = total + stage_01(index_value + 6, 7);
        }
    }
    while (total > limit * 2) {
        total = total - (6 + global_bias % 5);
    }
    return total;
}

int stage_04(int seed, int limit) {
    int total = stage_03(seed + 5, limit + 7);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 5;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_03(index_value + 5, 9);
        } else {
            total = total + stage_02(index_value + 7, 8);
        }
    }
    while (total > limit * 2) {
        total = total - (7 + global_bias % 5);
    }
    return total;
}

int stage_05(int seed, int limit) {
    int total = stage_04(seed + 6, limit + 3);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 6;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_04(index_value + 6, 5);
        } else {
            total = total + stage_03(index_value + 3, 4);
        }
    }
    while (total > limit * 2) {
        total = total - (3 + global_bias % 5);
    }
    return total;
}

int stage_06(int seed, int limit) {
    int total = stage_05(seed + 7, limit + 4);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 7;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_05(index_value + 7, 6);
        } else {
            total = total + stage_04(index_value + 4, 5);
        }
    }
    while (total > limit * 2) {
        total = total - (4 + global_bias % 5);
    }
    return total;
}

int stage_07(int seed, int limit) {
    int total = stage_06(seed + 8, limit + 5);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 8;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_06(index_value + 8, 7);
        } else {
            total = total + stage_05(index_value + 5, 6);
        }
    }
    while (total > limit * 2) {
        total = total - (5 + global_bias % 5);
    }
    return total;
}

int stage_08(int seed, int limit) {
    int total = stage_07(seed + 9, limit + 6);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 9;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_07(index_value + 9, 8);
        } else {
            total = total + stage_06(index_value + 6, 7);
        }
    }
    while (total > limit * 2) {
        total = total - (6 + global_bias % 5);
    }
    return total;
}

int stage_09(int seed, int limit) {
    int total = stage_08(seed + 10, limit + 7);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 10;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_08(index_value + 10, 9);
        } else {
            total = total + stage_07(index_value + 7, 8);
        }
    }
    while (total > limit * 2) {
        total = total - (7 + global_bias % 5);
    }
    return total;
}

int stage_10(int seed, int limit) {
    int total = stage_09(seed + 11, limit + 3);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 11;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_09(index_value + 11, 5);
        } else {
            total = total + stage_08(index_value + 3, 4);
        }
    }
    while (total > limit * 2) {
        total = total - (3 + global_bias % 5);
    }
    return total;
}

int stage_11(int seed, int limit) {
    int total = stage_10(seed + 12, limit + 4);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 12;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_10(index_value + 12, 6);
        } else {
            total = total + stage_09(index_value + 4, 5);
        }
    }
    while (total > limit * 2) {
        total = total - (4 + global_bias % 5);
    }
    return total;
}

int stage_12(int seed, int limit) {
    int total = stage_11(seed + 13, limit + 5);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 13;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_11(index_value + 13, 7);
        } else {
            total = total + stage_10(index_value + 5, 6);
        }
    }
    while (total > limit * 2) {
        total = total - (5 + global_bias % 5);
    }
    return total;
}

int stage_13(int seed, int limit) {
    int total = stage_12(seed + 14, limit + 6);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 14;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_12(index_value + 14, 8);
        } else {
            total = total + stage_11(index_value + 6, 7);
        }
    }
    while (total > limit * 2) {
        total = total - (6 + global_bias % 5);
    }
    return total;
}

int stage_14(int seed, int limit) {
    int total = stage_13(seed + 15, limit + 7);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 15;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_13(index_value + 15, 9);
        } else {
            total = total + stage_12(index_value + 7, 8);
        }
    }
    while (total > limit * 2) {
        total = total - (7 + global_bias % 5);
    }
    return total;
}

int stage_15(int seed, int limit) {
    int total = stage_14(seed + 16, limit + 3);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 16;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_14(index_value + 16, 5);
        } else {
            total = total + stage_13(index_value + 3, 4);
        }
    }
    while (total > limit * 2) {
        total = total - (3 + global_bias % 5);
    }
    return total;
}

int stage_16(int seed, int limit) {
    int total = stage_15(seed + 17, limit + 4);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 17;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_15(index_value + 17, 6);
        } else {
            total = total + stage_14(index_value + 4, 5);
        }
    }
    while (total > limit * 2) {
        total = total - (4 + global_bias % 5);
    }
    return total;
}

int stage_17(int seed, int limit) {
    int total = stage_16(seed + 18, limit + 5);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 18;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_16(index_value + 18, 7);
        } else {
            total = total + stage_15(index_value + 5, 6);
        }
    }
    while (total > limit * 2) {
        total = total - (5 + global_bias % 5);
    }
    return total;
}

int stage_18(int seed, int limit) {
    int total = stage_17(seed + 19, limit + 6);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 19;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_17(index_value + 19, 8);
        } else {
            total = total + stage_16(index_value + 6, 7);
        }
    }
    while (total > limit * 2) {
        total = total - (6 + global_bias % 5);
    }
    return total;
}

int stage_19(int seed, int limit) {
    int total = stage_18(seed + 20, limit + 7);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 20;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_18(index_value + 20, 9);
        } else {
            total = total + stage_17(index_value + 7, 8);
        }
    }
    while (total > limit * 2) {
        total = total - (7 + global_bias % 5);
    }
    return total;
}

int stage_20(int seed, int limit) {
    int total = stage_19(seed + 21, limit + 3);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 21;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_19(index_value + 21, 5);
        } else {
            total = total + stage_18(index_value + 3, 4);
        }
    }
    while (total > limit * 2) {
        total = total - (3 + global_bias % 5);
    }
    return total;
}

int stage_21(int seed, int limit) {
    int total = stage_20(seed + 22, limit + 4);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 22;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_20(index_value + 22, 6);
        } else {
            total = total + stage_19(index_value + 4, 5);
        }
    }
    while (total > limit * 2) {
        total = total - (4 + global_bias % 5);
    }
    return total;
}

int stage_22(int seed, int limit) {
    int total = stage_21(seed + 23, limit + 5);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 23;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_21(index_value + 23, 7);
        } else {
            total = total + stage_20(index_value + 5, 6);
        }
    }
    while (total > limit * 2) {
        total = total - (5 + global_bias % 5);
    }
    return total;
}

int stage_23(int seed, int limit) {
    int total = stage_22(seed + 24, limit + 6);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 24;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_22(index_value + 24, 8);
        } else {
            total = total + stage_21(index_value + 6, 7);
        }
    }
    while (total > limit * 2) {
        total = total - (6 + global_bias % 5);
    }
    return total;
}

int stage_24(int seed, int limit) {
    int total = stage_23(seed + 25, limit + 7);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 25;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_23(index_value + 25, 9);
        } else {
            total = total + stage_22(index_value + 7, 8);
        }
    }
    while (total > limit * 2) {
        total = total - (7 + global_bias % 5);
    }
    return total;
}

int stage_25(int seed, int limit) {
    int total = stage_24(seed + 26, limit + 3);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 26;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_24(index_value + 26, 5);
        } else {
            total = total + stage_23(index_value + 3, 4);
        }
    }
    while (total > limit * 2) {
        total = total - (3 + global_bias % 5);
    }
    return total;
}

int stage_26(int seed, int limit) {
    int total = stage_25(seed + 27, limit + 4);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 27;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_25(index_value + 27, 6);
        } else {
            total = total + stage_24(index_value + 4, 5);
        }
    }
    while (total > limit * 2) {
        total = total - (4 + global_bias % 5);
    }
    return total;
}

int stage_27(int seed, int limit) {
    int total = stage_26(seed + 28, limit + 5);
    int values[8];
    int index_value = 0;
    for (index_value = 0; index_value < 8; index_value = index_value + 1) {
        values[index_value] = total + index_value + 28;
        total = total + values[index_value];
        if (total > limit && seed > 0) {
            total = total - stage_26(index_value + 28, 7);
        } else {
            total = total + stage_25(index_value + 5, 6);
        }
    }
    while (total > limit * 2) {
        total = total - (5 + global_bias % 5);
    }
    return total;
}

int main() {
    int accumulator = 0;
    int index = 0;
    while (index < 12) {
        global_values[index] = stage_27(index + global_bias, 12 + index);
        accumulator = accumulator + global_values[index];
        if (accumulator % 2 == 0) {
            accumulator = accumulator + stage_10(index + 3, 9);
        } else {
            accumulator = accumulator - stage_05(index + 1, 7);
        }
        index = index + 1;
    }
    if (accumulator > 5000) {
        accumulator = accumulator - global_bias;
    } else {
        accumulator = accumulator + 11;
    }
    return accumulator % 97;
}
