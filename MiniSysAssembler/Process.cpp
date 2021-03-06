/*
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

Instruction* cur_instruction = nullptr;
unsigned cur_address = 0;

int GeneratedMachineCode(InstructionList& instruction_list,
                         UnsolvedSymbolMap& unsolved_symbol_map,
                         SymbolMap& symbol_map) {
    unsigned int address = 0;
    bool meet_error = 0;
    cur_address = 0;
    for (auto& instruction : instruction_list) {
        if (instruction.done) {
            address = cur_address =
                instruction.address + 4 * instruction.machine_code.size();
            continue;
        }
        assert(instruction.machine_code.size() == 0);
        cur_instruction = &instruction;
        // 去除标号及注释，标号加入符号表中，返回的汇编无空白前缀
        std::string assembly = toUppercase(
            ProcessLabel(address, instruction.assembly, symbol_map));
        instruction.address = address;
        if (assembly != "") {
            try {
                ProcessInstruction(assembly, instruction, unsolved_symbol_map);
            } catch (const std::exception& e) {
                Error(e.what(), &instruction);
                cur_instruction = nullptr;
                meet_error = 1;
            }
            address = cur_address;
        }
        instruction.done = true;
        cur_instruction = nullptr;
    }
    return meet_error;
}

int GeneratedDataSegment(DataList& data_list,
                         UnsolvedSymbolMap& unsolved_symbol_map,
                         SymbolMap& symbol_map) {
    unsigned int address = 0;
    bool meet_error = 0;
    cur_address = 0;
    for (auto& data : data_list) {
        if (data.done) {
            address = cur_address = data.address + data.raw_data.size();
            continue;
        }
        assert(data.raw_data.size() == 0);
        cur_instruction = (Instruction*)&data;
        std::string assembly =
            toUppercase(ProcessLabel(address, data.assembly, symbol_map));
        data.address = address;
        if (assembly != "") {
            try {
                ProcessData(assembly, data, unsolved_symbol_map);
            } catch (const std::exception& e) {
                Error(e.what());
                cur_instruction = nullptr;
                meet_error = 1;
            }

            address = cur_address;
        }
        data.done = true;
    }
    return meet_error;
}

void ProcessInstruction(const std::string& assembly, Instruction& instruction,
                        UnsolvedSymbolMap& unsolved_symbol_map) {
    if (assembly != "") {
        std::string mnemonic = GetMnemonic(assembly);
        if (isR_Format(mnemonic)) {  // R-format
            MachineCodeHandle handel = NewMachineCode(instruction);
            R_FormatInstruction(toUppercase(mnemonic), assembly,
                                unsolved_symbol_map, handel);
            cur_address += 4;
        } else if (isI_Format(mnemonic)) {  // I-format
            MachineCodeHandle handel = NewMachineCode(instruction);
            I_FormatInstruction(toUppercase(mnemonic), assembly,
                                unsolved_symbol_map, handel);
            cur_address += 4;
        } else if (isJ_Format(mnemonic)) {  // J-format
            MachineCodeHandle handel = NewMachineCode(instruction);
            J_FormatInstruction(toUppercase(mnemonic), assembly,
                                unsolved_symbol_map, handel);
            cur_address += 4;
        } else if (isMacro_Format(mnemonic)) {  // 宏指令
            MachineCodeHandle handel = NewMachineCode(instruction);
            Macro_FormatInstruction(toUppercase(mnemonic), assembly,
                                    unsolved_symbol_map, handel);
            cur_address += 4;
        } else {
            throw UnkonwInstruction(mnemonic);
        }
    }
}

void ProcessData(const std::string& assembly, Data& data,
                 UnsolvedSymbolMap& unsolved_symbol_map) {
    if (assembly != "") {
        std::regex re(R"(^\.(BYTE|HALF|WORD)\s+(.+)$)", std::regex::icase);
        std::cmatch m;
        std::regex_search(assembly.c_str(), m, re);
        if (!m.empty()) {
            const std::string type = m[1].str();
            if (type == "BYTE" || type == "HALF" || type == "WORD") {
                std::string datastr = m[2].str();
                std::regex re(R"(^([^:,\s]+)\s*(?:\:\s*([^:,\s]+))?(\s*,\s*)?)",
                              std::regex::icase);
                do {
					std::cmatch m;
                    std::regex_search(datastr.c_str(), m, re);
                    std::string cur_data_str = m[1].str(),
                                repeat_time_str = m[2].str();
                    datastr = m.suffix();
                    unsigned repeat_time = 1;
                    if (m[2].matched) {
                        if (isPositive(repeat_time_str)) {
                            repeat_time = toUNumber(repeat_time_str);
                        } else {
                            throw ExceptPositive(repeat_time_str);
                        }
                    }
                    if (!isNumber(cur_data_str)) {
                        throw ExceptNumber(cur_data_str);
                    }
                    std::uint32_t d = toNumber(cur_data_str);
                    for (unsigned i = 0; i < repeat_time; i++) {
                        if (type == "BYTE") {
                            data.raw_data.push_back(d & 0xff);
                            cur_address += 1;
                        } else if (type == "HALF") {
                            data.raw_data.push_back(d & 0xff);
                            data.raw_data.push_back((d >> 8) & 0xff);
                            cur_address += 2;
                        } else if (type == "WORD") {
                            data.raw_data.push_back(d & 0xff);
                            data.raw_data.push_back((d >> 8) & 0xff);
                            data.raw_data.push_back((d >> 16) & 0xff);
                            data.raw_data.push_back(d >> 24);
                            cur_address += 4;
                        } else {
                            throw std::runtime_error("Unkonw error.");
                        }
                    }
                } while (datastr != "");
            } else {
                throw UnkonwInstruction(type);
            }
        }
    }
}
std::string ProcessLabel(unsigned int address, const std::string& assembly,
                         SymbolMap& symbol_map) {
    static std::regex re("\\s*(?:(\\S+?)\\s*:)?\\s*(.*?)\\s*(?:#.*)?");
    std::smatch match;
    std::string assembly2 = KillComment(assembly);
    std::regex_match(assembly2, match, re);
    if (match[1].matched) {
        std::string label = toUppercase(match[1].str());
        if (symbol_map.find(label) != symbol_map.end()) {
            throw std::runtime_error("Redefine symbol:" + label);
        } else {
            symbol_map[label] = address;
        }
    }
    return match[2].str();
}

std::string KillComment(const std::string& assembly) {
    static std::regex re("^([^#]*)(?:#.*)?");
    std::smatch match;
    std::regex_match(assembly, match, re);
    return match[1].str();
}

int SolveSymbol(UnsolvedSymbolMap& unsolved_symbol_map,
                const SymbolMap& symbol_map) {
    for (auto it = unsolved_symbol_map.begin(); it != unsolved_symbol_map.end();
         it++) {
        std::string symbol = it->first;
        for (auto& ref : it->second) {
            try {
                cur_instruction = ref.instruction;
                cur_address = cur_instruction->address;
                if (symbol_map.find(symbol) == symbol_map.end()) {
                    throw std::runtime_error("Unknow Symbol: " + symbol + ".");
                }
                MachineCode& machine_code = *ref.machine_code_handle;
                if (isR_Format(machine_code)) {  // R-format 符号只可能在shamt
                    int shamt = symbol_map.at(symbol);
                    SetShamt(machine_code, shamt);
                } else if (isI_Format(machine_code)) {
                    int imm = symbol_map.at(symbol);
                    int op = machine_code >> 26;
                    if (op == 0b000100 || op == 0b000101 || op == 0b000001 ||
                        op == 0b000111 || op == 0b000110) {
                        // 转跳指令
                        imm -= cur_address + 4;
                        imm >>= 2;
                    }
                    SetImmediate(machine_code, imm);
                } else if (isJ_Format(machine_code)) {
                    int addr = symbol_map.at(symbol) >> 2;
                    SetAddress(machine_code, addr);
                } else {
                    throw std::runtime_error("Unknow errors.");
                }
                cur_instruction = nullptr;
            } catch (const std::exception& e) {
                Error(e.what());
                if (symbol_map.find(symbol) != symbol_map.end()) {
                    Note(
                        "This error occurs while solving symbol. At this "
                        "time, " +
                        symbol + " = " + std::to_string(symbol_map.at(symbol)));
                }
                cur_instruction = nullptr;
                return 1;
            }
        }
    }
    return 0;
}
