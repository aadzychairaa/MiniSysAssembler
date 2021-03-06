﻿/*
 * Copyright 2019 nzh63
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "pch.h"

int doAssemble(const std::string &input_file_path,
               const std::string &output_folder_path, unsigned options) {
    bool meet_error = 0;
    std::fstream file;
    file.open(input_file_path, std::ios_base::in);
    if (!file.is_open()) {
        Error("fail to open file: " + input_file_path);
        return 1;
    }
    InstructionList instruction_list;
    DataList data_list;
    int line = 0;
    enum { global, data, text };
    int state = global;
    while (!file.eof()) {
        line++;
        std::string input;
        getline(file, input);
        input = KillComment(input);
        std::regex re(R"(^\s*\.(data|text)\s*(\S+)?)", std::regex::icase);
        std::cmatch m;
        std::regex_search(input.c_str(), m, re);
        // 从这一行到91行写得很差，有时间重写一遍
        if (!m.empty()) {
            if (toUppercase(m[1].str()) == "DATA") {
                state = data;
                if (m[2].matched) {
                    if (isPositive(m[2].str())) {
                        unsigned pos = toUNumber(m[2].str());
                        Data data;
                        data.file = input_file_path;
                        data.line = line;
                        data.assembly = input;
                        for (unsigned i = 0; i < pos; i++) {
                            data.raw_data.push_back(0);
                        }
                        data.address = 0;
                        data.done = true;
                        data_list.push_back(data);
                    } else {
                        Msg("Need a number.",
                            input_file_path + '(' + std::to_string(line) + ')',
                            LogLevel::error);
                        return 1;
                    }
                }
            } else {
                state = text;
                if (m[2].matched) {
                    if (isPositive(m[2].str())) {
                        unsigned pos = toUNumber(m[2].str());
                        if (pos % 4 != 0) {
                            Msg("DWORD-aligned error.",
                                input_file_path + '(' + std::to_string(line) +
                                    ')',
                                LogLevel::error);
                            return 1;
                        }
                        Instruction instruction;
                        instruction.file = input_file_path;
                        instruction.line = line;
                        instruction.assembly = input;
                        for (unsigned i = 0; i < pos / 4; i++) {
                            instruction.machine_code.push_back(0);  // nop
                        }
                        instruction.address = 0;
                        instruction.done = true;
                        instruction_list.push_back(instruction);
                    } else {
                        Msg("Need a number.",
                            input_file_path + '(' + std::to_string(line) + ')',
                            LogLevel::error);
                        return 1;
                    }
                }
            }
            continue;
        }
        if (state == global) {
            Msg("Need a segment.",
                input_file_path + '(' + std::to_string(line) + ')',
                LogLevel::error);
            return 1;
        } else if (state == text) {
            Instruction instruction;
            instruction.file = input_file_path;
            instruction.line = line;
            instruction.assembly = input;
            instruction_list.push_back(instruction);
        } else if (state == data) {
            Data data;
            data.file = input_file_path;
            data.line = line;
            data.assembly = input;
            data_list.push_back(data);
        }
    }
    file.close();

    UnsolvedSymbolMap unsolved_symbol_map;
    SymbolMap symbol_map;
    if (GeneratedDataSegment(data_list, unsolved_symbol_map, symbol_map)) {
        meet_error = 1;
    }
    if (GeneratedMachineCode(instruction_list, unsolved_symbol_map,
                             symbol_map)) {
        meet_error = 1;
    }
    if (!meet_error && SolveSymbol(unsolved_symbol_map, symbol_map)) {
        meet_error = 1;
    }
    if (!meet_error) {
        file.open(output_folder_path + "prgmip32.coe", std::ios_base::out);
        if (file.is_open()) {
            OutputInstruction(file, instruction_list);
        } else {
            Error("fail to open file: " + output_folder_path + "prgmip32.coe");
        }
        file.close();

        file.open(output_folder_path + "dmem32.coe", std::ios_base::out);
        if (file.is_open()) {
            OutputDataSegment(file, data_list);
        } else {
            Error("fail to open file: " + output_folder_path + "dmem32.coe");
        }
        file.close();

        if (options | allow_options.at("--show-details")) {
            file.open(output_folder_path + "details.txt", std::ios_base::out);
            if (file.is_open()) {
                ShowDetails(instruction_list, data_list, file);
            } else {
                Error("fail to open file: " + output_folder_path +
                      "details.txt");
            }
            file.close();
        }
    }
    return meet_error;
}
