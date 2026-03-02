#include "lexer.hpp"
#include "encoder.hpp"

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

struct FunctionMetadata {
    std::string name = "main";
    uint16_t arg_count = 0;
    uint16_t var_count = 0;
    uint16_t stack_size = 4;
    bool strict_mode = true;
};

FunctionMetadata parse_metadata(const std::vector<Token>& tokens) {
    FunctionMetadata meta;
    
    for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i].type == Token::DIRECTIVE) {
            const std::string& directive = tokens[i].value;
            
            if (directive == ".function" && i + 1 < tokens.size()) {
                i++;
                if (tokens[i].type == Token::OPCODE || tokens[i].type == Token::LABEL) {
                    meta.name = tokens[i].value;
                }
            } else if (directive == ".args" && i + 1 < tokens.size() && tokens[i + 1].type == Token::NUMBER) {
                meta.arg_count = std::stoi(tokens[++i].value);
            } else if ((directive == ".vars" || directive == ".locals") && i + 1 < tokens.size() && tokens[i + 1].type == Token::NUMBER) {
                meta.var_count = std::stoi(tokens[++i].value);
            } else if (directive == ".stack" && i + 1 < tokens.size() && tokens[i + 1].type == Token::NUMBER) {
                meta.stack_size = std::stoi(tokens[++i].value);
            } else if (directive == ".mode" && i + 1 < tokens.size()) {
                i++;
                if (tokens[i].value == "strict") {
                    meta.strict_mode = true;
                } else if (tokens[i].value == "sloppy") {
                    meta.strict_mode = false;
                }
            }
        }
    }
    
    return meta;
}

// Wrap bytecode body in QuickJS function format (following bcwriter.hpp structure)
bytes wrap_function_bytecode(const bytes& bytecode_body, const FunctionMetadata& meta) {
    bytes result;
    
    // Version byte
    result.push_back(0x05); // BC_VERSION = 5
    
    // Atom table (atoms used in the function)
    encode_leb128_u(result, 1); // atom count (just function name for now)
    encode_string(result, meta.name); // atom string
    
    // Function bytecode tag
    result.push_back(0x0c); // BC_TAG_FUNCTION_BYTECODE
    
    // Function header (following bcwriter.hpp order)
    uint16_t flags = 0x0001; // Minimal flags for now (was 0x643 in bcwriter)
    encode_u16(result, flags);
    encode_u8(result, 1); // jsMode (1 = strict, matching flags)
    
    // Function metadata (all LEB128 encoded)
    encode_atom(result, 0); // function name atom index
    encode_leb128_u(result, meta.arg_count);
    encode_leb128_u(result, meta.var_count); // Note: bcwriter uses locals.size() here
    encode_leb128_u(result, meta.arg_count); // defined_arg_count
    encode_leb128_u(result, meta.stack_size);
    encode_leb128_u(result, 0); // var_ref_count
    encode_leb128_i(result, 0); // closure_var_count (signed)
    encode_leb128_i(result, 0); // cpool_count (signed)
    encode_leb128_i(result, bytecode_body.size()); // byte_code_len (signed)
    encode_leb128_i(result, meta.var_count + meta.arg_count); // local_count (signed)
    
    // Local variable definitions (args + locals)
    // Each local needs: atom (leb128), scopeLevel (leb128), scopeNext (leb128), flags (u8)
    // For args:
    for (uint32_t i = 0; i < meta.arg_count; i++) {
        encode_atom(result, 0); // atom (reuse function name for now)
        encode_leb128_u(result, 0); // scopeLevel
        encode_leb128_u(result, i + 1); // scopeNext
        encode_u8(result, 0); // flags
    }
    
    // For locals:
    for (uint32_t i = 0; i < meta.var_count; i++) {
        encode_atom(result, 0); // atom (reuse function name for now)
        encode_leb128_u(result, 0); // scopeLevel
        encode_leb128_u(result, meta.arg_count + i + 1); // scopeNext
        encode_u8(result, 0); // flags
    }
    
    // Closure variables would go here (if closure_var_count > 0)
    
    // Append bytecode
    result.insert(result.end(), bytecode_body.begin(), bytecode_body.end());
    
    // Note: Debug info (filename, pc2line, source) should come here according to bcwriter.hpp
    // but we're omitting it for now since our original version worked without it
    
    // Constant pool would go here (if cpool_count > 0)
    
    return result;
}

bytes assemble(const std::string& content) {
    auto tokens = tokenize(content);
    auto opcode_map = build_opcode_map();
    
    // Parse metadata first
    FunctionMetadata meta = parse_metadata(tokens);
    
    bytes bytecode_body;
    std::map<std::string, size_t> labels;
    std::vector<std::tuple<size_t, std::string, std::string>> fixups;
    
    // First pass: collect labels and generate code
    size_t pc = 0;
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
            // Skip directives (already parsed)
            i++;
            if (i < tokens.size() && (tokens[i].type == Token::NUMBER || 
                                      tokens[i].type == Token::OPCODE || 
                                      tokens[i].type == Token::LABEL)) {
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
            bytecode_body.push_back(info.opcode);
            pc++;
            
            // Handle operands
            if (info.format != "none" && info.format != "none_int" && 
                info.format != "none_loc" && info.format != "none_arg" && 
                info.format != "none_var_ref" && info.format != "npopx" && 
                info.format != "npop") {
                
                i++;
                if (i >= tokens.size() || (tokens[i].type != Token::NUMBER && tokens[i].type != Token::LABEL)) {
                    throw std::runtime_error("Missing operand for " + token.value + " at line " + std::to_string(token.line));
                }
                
                const auto& operand = tokens[i];
                
                if (info.format == "i32" || info.format == "const") {
                    if (operand.type == Token::NUMBER) {
                        int32_t val = std::stoi(operand.value);
                        encode_i32(bytecode_body, val);
                        pc += 4;
                    } else {
                        throw std::runtime_error("Expected number for " + info.format);
                    }
                } else if (info.format == "u32") {
                    if (operand.type == Token::NUMBER) {
                        uint32_t val = std::stoul(operand.value);
                        encode_u32(bytecode_body, val);
                        pc += 4;
                    } else {
                        throw std::runtime_error("Expected number for " + info.format);
                    }
                } else if (info.format == "i16" || info.format == "u16" || 
                           info.format == "loc" || info.format == "arg" || 
                           info.format == "var_ref" || info.format == "npop_u16") {
                    if (operand.type == Token::NUMBER) {
                        int16_t val = std::stoi(operand.value);
                        encode_i16(bytecode_body, val);
                        pc += 2;
                    } else {
                        throw std::runtime_error("Expected number for " + info.format);
                    }
                } else if (info.format == "i8" || info.format == "u8" || 
                           info.format == "loc8" || info.format == "const8") {
                    if (operand.type == Token::NUMBER) {
                        int8_t val = std::stoi(operand.value);
                        encode_i8(bytecode_body, val);
                        pc += 1;
                    } else {
                        throw std::runtime_error("Expected number for " + info.format);
                    }
                } else if (info.format == "label" || info.format == "label16" || info.format == "label8") {
                    size_t fixup_offset = bytecode_body.size();
                    if (info.format == "label") {
                        encode_i32(bytecode_body, 0);
                        pc += 4;
                    } else if (info.format == "label16") {
                        encode_i16(bytecode_body, 0);
                        pc += 2;
                    } else {
                        encode_i8(bytecode_body, 0);
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
            int32_t rel = target - (offset + 4);
            *reinterpret_cast<int32_t*>(&bytecode_body[offset]) = rel;
        } else if (format == "label16") {
            int16_t rel = target - (offset + 2);
            *reinterpret_cast<int16_t*>(&bytecode_body[offset]) = rel;
        } else if (format == "label8") {
            int8_t rel = target - (offset + 1);
            bytecode_body[offset] = static_cast<uint8_t>(rel);
        }
    }
    
    // Wrap bytecode in function format
    return wrap_function_bytecode(bytecode_body, meta);
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
