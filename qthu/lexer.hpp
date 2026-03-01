#pragma once

#include <string>
#include <vector>

struct Token {
    enum Type { OPCODE, NUMBER, LABEL, LABEL_DEF, DIRECTIVE, COMMENT, END };
    Type type;
    std::string value;
    int line;
};

std::vector<Token> tokenize(const std::string& content);
