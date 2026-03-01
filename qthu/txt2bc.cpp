#include "lexer.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <ios>
#include <sstream>
#include <cstdint>
#include <map>
#include <cctype>
#include <cstring>

using bytes = std::vector<uint8_t>;
using sv_t = std::string_view;

struct Config 
{
    std::string file_in;
    std::string file_out;
};

struct OPCodeInfo
{
    std::uint8_t opcode;
    int size;
    std::string format;
};

std::map<std::string, OPCodeInfo> build_opcode_map()
{
    std::map<std::string, OPCodeInfo> opcode_map;
    uint8_t opcode_id = 0;
    
    #define DEF(id, size, n_pop, n_push, f) \
        opcode_map[#id] = OPCodeInfo{opcode_id++, size, #f};
    #define def(id, size, n_pop, n_push, f) \
        opcode_map[#id] = OPCodeInfo{opcode_id++, size, #f};
    
    #include "../quickjs-opcode.h"
    
    #undef DEF
    #undef def
    
    return opcode_map;
}

// Encode integer in little-endian
void encode_u8(bytes& buf, uint8_t val) {
    buf.push_back(val);
}

void encode_i8(bytes& buf, int8_t val) {
    buf.push_back(static_cast<uint8_t>(val));
}

void encode_u16(bytes& buf, uint16_t val) {
    buf.push_back(val & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
}

void encode_i16(bytes& buf, int16_t val) {
    encode_u16(buf, static_cast<uint16_t>(val));
}

void encode_u32(bytes& buf, uint32_t val) {
    buf.push_back(val & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
    buf.push_back((val >> 16) & 0xFF);
    buf.push_back((val >> 24) & 0xFF);
}

void encode_i32(bytes& buf, int32_t val) {
    encode_u32(buf, static_cast<uint32_t>(val));
}

bytes assemble(const std::string& content) {
    auto tokens = tokenize(content);
    auto opcode_map = build_opcode_map();
    
    bytes result;
    std::map<std::string, size_t> labels;
    std::vector<std::tuple<size_t, std::string, std::string>> fixups; // offset, label, format
    
    // First pass: collect labels and generate code
    size_t pc = 0; // program counter
    size_t i = 0;
    
    while (i < tokens.size()) {
        const auto& token = tokens[i];
        
        if (token.type == Token::END) break;
        
        if (token.type == Token::LABEL_DEF) {
            labels[token.value] = pc;
            i++;
            continue;
        }
        
        if (token.type == Token::DIRECTIVE) {
            // Skip directives in first pass (they're for function metadata)
            i++;
            // Skip directive argument if present
            if (i < tokens.size() && tokens[i].type == Token::NUMBER) {
                i++;
            }
            continue;
        }
        
        if (token.type == Token::OPCODE) {
            auto it = opcode_map.find(token.value);
            if (it == opcode_map.end()) {
                throw std::runtime_error("Unknown opcode: " + token.value + " at line " + std::to_string(token.line));
            }
            
            const auto& info = it->second;
            result.push_back(info.opcode);
            pc++;
            
            // Handle operands based on format
            if (info.format != "none" && info.format != "none_int" && 
                info.format != "none_loc" && info.format != "none_arg" && 
                info.format != "none_var_ref" && info.format != "npopx" && 
                info.format != "npop") {
                
                i++; // Move to operand token
                if (i >= tokens.size() || (tokens[i].type != Token::NUMBER && tokens[i].type != Token::LABEL)) {
                    throw std::runtime_error("Missing operand for " + token.value + " at line " + std::to_string(token.line));
                }
                
                const auto& operand = tokens[i];
                
                if (info.format == "i32" || info.format == "const") {
                    if (operand.type == Token::NUMBER) {
                        int32_t val = std::stoi(operand.value);
                        encode_i32(result, val);
                        pc += 4;
                    } else {
                        throw std::runtime_error("Expected number for " + info.format);
                    }
                } else if (info.format == "u32") {
                    if (operand.type == Token::NUMBER) {
                        uint32_t val = std::stoul(operand.value);
                        encode_u32(result, val);
                        pc += 4;
                    } else {
                        throw std::runtime_error("Expected number for " + info.format);
                    }
                } else if (info.format == "i16" || info.format == "u16" || 
                           info.format == "loc" || info.format == "arg" || 
                           info.format == "var_ref" || info.format == "npop_u16") {
                    if (operand.type == Token::NUMBER) {
                        int16_t val = std::stoi(operand.value);
                        encode_i16(result, val);
                        pc += 2;
                    } else {
                        throw std::runtime_error("Expected number for " + info.format);
                    }
                } else if (info.format == "i8" || info.format == "u8" || 
                           info.format == "loc8" || info.format == "const8") {
                    if (operand.type == Token::NUMBER) {
                        int8_t val = std::stoi(operand.value);
                        encode_i8(result, val);
                        pc += 1;
                    } else {
                        throw std::runtime_error("Expected number for " + info.format);
                    }
                } else if (info.format == "label" || info.format == "label16" || info.format == "label8") {
                    // Record fixup for later
                    size_t fixup_offset = result.size();
                    if (info.format == "label") {
                        encode_i32(result, 0); // placeholder
                        pc += 4;
                    } else if (info.format == "label16") {
                        encode_i16(result, 0); // placeholder
                        pc += 2;
                    } else { // label8
                        encode_i8(result, 0); // placeholder
                        pc += 1;
                    }
                    
                    if (operand.type == Token::LABEL) {
                        fixups.push_back({fixup_offset, operand.value, info.format});
                    } else {
                        throw std::runtime_error("Expected label for " + info.format);
                    }
                }
            }
            
            i++;
        } else {
            i++;
        }
    }
    
    // Second pass: resolve labels
    for (const auto& [offset, label, format] : fixups) {
        auto it = labels.find(label);
        if (it == labels.end()) {
            throw std::runtime_error("Undefined label: " + label);
        }
        
        size_t target = it->second;
        
        if (format == "label") {
            // Calculate relative offset from end of instruction
            int32_t rel = target - (offset + 4);
            *reinterpret_cast<int32_t*>(&result[offset]) = rel;
        } else if (format == "label16") {
            int16_t rel = target - (offset + 2);
            *reinterpret_cast<int16_t*>(&result[offset]) = rel;
        } else if (format == "label8") {
            int8_t rel = target - (offset + 1);
            result[offset] = static_cast<uint8_t>(rel);
        }
    }
    
    return result;
}

Config parse_config( int argc, char* argv[] ) 
{
    std::string in = argv[ 1 ];
    if ( argc == 2 )
        return Config{ argv[ 1 ], std::string("out.qbc") };
    
    return Config{ argv[ 1 ], argv[ 3 ] };

} 

std::string read_file( std::ifstream& file )
{
    std::stringstream sstream;
    sstream << file.rdbuf();
    return sstream.str();
}

int main(int argc, char* argv[])
{
    if (argc != 2 && argc != 4) {
        std::cout << "Usage: txt2bc in_file.txt [-o out_file.qbc]\n";
        return -1;
    }

    if (argc == 4 && std::string(argv[2]) != "-o") {
        std::cout << "Wrong flag: " << argv[2] << ", use -o to specify output \n";
        return -1;
    }

    Config config = parse_config(argc, argv);

    std::ifstream file(config.file_in, std::ios::in);
    if (!file.is_open()) {
        std::cout << "Couldn't open " << config.file_in << " \n";
        return -1;
    }

    std::string content = read_file(file);
    
    try {
        bytes bytecode = assemble(content);

        if (bytecode.empty()) {
            std::cout << "No bytecode generated\n";
            return -1;
        }

        std::ofstream out(config.file_out, std::ios::out | std::ios::binary);
        if (!out.is_open()) {
            std::cout << "Couldn't open output file " << config.file_out << '\n';
            return -1;
        }

        out.write(reinterpret_cast<const char*>(bytecode.data()), bytecode.size());
        
        std::cout << "Successfully converted " << content.size() << " bytes of text to "
                  << bytecode.size() << " bytes of bytecode\n";
        std::cout << "Output written to: " << config.file_out << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}