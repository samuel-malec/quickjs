#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <cctype>

struct Token 
{
    enum Type { OPCODE, NUMBER, LABEL, LABEL_DEF, DIRECTIVE, COMMENT, END };
    Type type;
    std::string value;
    int line;
};

std::vector< Token > tokenize( const std::string& content )
{
    std::vector< Token > tokens;
    std::istringstream stream( content );
    std::string line;
    int line_num = 0;
    
    while ( std::getline( stream, line ) )
    {
        line_num++;
        
        size_t comment_pos = line.find( ';' );
        if ( comment_pos != std::string::npos ) {
            line = line.substr( 0, comment_pos );
        }
        
        size_t start = line.find_first_not_of( " \t\r\n" );
        if ( start == std::string::npos ) continue;
        
        line = line.substr(start);
        size_t end = line.find_last_not_of( " \t\r\n" );
        line = line.substr( 0, end + 1 );
        
        if ( line.empty() ) continue;
        
        if ( line.back() == ':' )
        {
            tokens.push_back( { Token::LABEL_DEF, line.substr( 0, line.size() - 1 ), line_num } );
            continue;
        }
        
        if ( line[ 0 ] == '.' )
        {
            size_t space = line.find( ' ' );
            if ( space == std::string::npos )
            {
                tokens.push_back({Token::DIRECTIVE, line, line_num});
            } 
            
            else 
            {
                tokens.push_back( { Token::DIRECTIVE, line.substr( 0, space ), line_num } );
                std::string rest = line.substr( space + 1 );
                size_t pos = rest.find_first_not_of( " \t" );
                if ( pos != std::string::npos )
                {
                    rest = rest.substr( pos );
                    size_t end = rest.find_first_of( " \t" );
                    std::string arg = ( end != std::string::npos ) ? rest.substr( 0, end ) : rest;
                    if ( std::isdigit( arg[ 0 ] ) || ( arg[ 0 ] == '-' && arg.length() > 1 && std::isdigit( arg[ 1 ] ) ) )
                    {
                        tokens.push_back( { Token::NUMBER, arg, line_num } );
                    }
                    else 
                    {
                        tokens.push_back( { Token::OPCODE, arg, line_num } );
                    }
                }
            }
            continue;
        }
        
        std::istringstream line_stream( line );
        std::string opcode;
        line_stream >> opcode;
        
        tokens.push_back( { Token::OPCODE, opcode, line_num } );
        
        std::string operand;
        if ( line_stream >> operand )
        {
            if ( std::isdigit( operand[ 0 ] ) || operand[ 0 ] == '-' )
            {
                tokens.push_back( { Token::NUMBER, operand, line_num } );
            } 
            else
            {
                tokens.push_back( { Token::LABEL, operand, line_num } );
            }
        }
    }
    
    tokens.push_back( { Token::END, "", line_num } );
    return tokens;
}
