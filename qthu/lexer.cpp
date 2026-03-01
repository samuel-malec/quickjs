#include "lexer.hpp"

#include <sstream>
#include <cctype>

std::vector<Token> tokenize(const std::string& content) {
    std::vector<Token> tokens;
    std::istringstream stream(content);
    std::string line;
    int line_num = 0;
    
    while (std::getline(stream, line)) {
        line_num++;
        
        size_t comment_pos = line.find(';');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        
        line = line.substr(start);
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(0, end + 1);
        
        if (line.empty()) continue;
        
        // Check for label definition (ends with ':')
        if (line.back() == ':') {
            tokens.push_back({Token::LABEL_DEF, line.substr(0, line.size() - 1), line_num});
            continue;
        }
        
        // Check for directive (starts with '.')
        if (line[0] == '.') {
            size_t space = line.find(' ');
            if (space == std::string::npos) {
                tokens.push_back({Token::DIRECTIVE, line, line_num});
            } else {
                tokens.push_back({Token::DIRECTIVE, line.substr(0, space), line_num});
                // Parse directive arguments
                std::string rest = line.substr(space + 1);
                size_t pos = rest.find_first_not_of(" \t");
                if (pos != std::string::npos) {
                    tokens.push_back({Token::NUMBER, rest.substr(pos), line_num});
                }
            }
            continue;
        }
        
        // Parse opcode with optional operand
        std::istringstream line_stream(line);
        std::string opcode;
        line_stream >> opcode;
        
        tokens.push_back({Token::OPCODE, opcode, line_num});
        
        // Check for operand
        std::string operand;
        if (line_stream >> operand) {
            // Check if it's a number or label
            if (std::isdigit(operand[0]) || operand[0] == '-') {
                tokens.push_back({Token::NUMBER, operand, line_num});
            } else {
                tokens.push_back({Token::LABEL, operand, line_num});
            }
        }
    }
    
    tokens.push_back({Token::END, "", line_num});
    return tokens;
}
