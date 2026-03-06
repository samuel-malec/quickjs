#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <cctype>
#include <string_view>
#include <cassert>
#include <map>
#include <cstdint>
#include "opcodes.hpp"

struct Location {
    int line;
    int col;
};

struct Token 
{
    enum Category { OPCODE, FUNC_NAME, NUMBER, LABEL, LABEL_DEF, DIRECTIVE, COMMENT, END };
    Category cat;
    Location loc;
    std::string data;
};

struct Lexer 
{
    using sv_t = std::string_view;
    
    Location loc{ 1, 1 };
    std::vector< Token > tokens{};
    sv_t data{};
    std::size_t ptr = 0;

    explicit Lexer( sv_t input ) : data( input ) {}

    std::vector< Token > lex() 
    {
        tokens.clear();
        while ( !is_end() )
            get_next();
        
        add_token( Token::END, ptr, ptr );
        return tokens;
    }

    void get_next()
    {
        if ( is_end() )
            return;

        char c = peek();
        if ( std::isspace( static_cast<unsigned char>( c ) ) )
        {
            advance();
            return;
        }

        if ( c == ';' )
        {
            std::size_t start = ptr;
            while ( !is_end() && peek() != '\n' )
                advance();
            add_token( Token::COMMENT, start, ptr );
            return;
        }

        std::size_t start = ptr;
        while ( !is_end() )
        {
            char p = peek();
            if ( std::isspace( static_cast< unsigned char >( p ) ) || p == ';' )
                break;
            advance();
        }

        sv_t word = data.substr( start, ptr - start );
        if ( word.empty() )
            return;

        if ( word.back() == ':' )
        {
            add_token( Token::LABEL_DEF, start, ptr - 1 );
            return;
        }

        if ( c == '.' )
        {
            add_token( Token::DIRECTIVE, start, ptr );
            return;
        }

        if ( is_number( word ) )
        {
            add_token( Token::NUMBER, start, ptr );
            return;
        }

        if ( should_be_label() )
            add_token( Token::LABEL, start, ptr );
        else if ( get_opcode_set().count( std::string( word ) ) )
            add_token( Token::OPCODE, start, ptr );
        else
            add_token( Token::FUNC_NAME, start, ptr );
    }

    char peek() const 
    {
        assert( ptr < data.size() );
        return data[ ptr ];
    }

    bool is_end() const
    {
        return ptr >= data.size();
    }

    void advance()
    {
        if ( !is_end() )
            ptr++;
    }

    void add_token( Token::Category cat, std::size_t start, std::size_t end )
    {
        tokens.push_back( { cat, loc, std::string( data.substr( start, end - start ) ) } );
        drop();
    }

    bool is_number( sv_t s ) const
    {
        if ( s.empty() )
            return false;

        std::size_t i = 0;
        if ( s[ 0 ] == '-' )
        {
            if ( s.size() == 1 )
                return false;
            i = 1;
        }

        for ( ; i < s.size(); i++ )
        {
            if ( !std::isdigit( static_cast<unsigned char>( s[ i ] ) ) )
                return false;
        }
        return true;
    }

    bool should_be_label() const
    {
        if ( tokens.empty() )
            return false;

        const Token& prev = tokens.back();
        if ( prev.cat != Token::OPCODE )
            return false;

        return prev.data == "if_false" || prev.data == "if_true" ||
               prev.data == "goto" || prev.data == "goto8" ||
               prev.data == "goto16" || prev.data == "if_false8" ||
               prev.data == "if_true8";
    }

    void drop()
    {
        for ( std::size_t i = 0; i < ptr && i < data.size(); i++ )
        {
            if ( data[ i ] == '\n' )
            {
                loc.line++;
                loc.col = 1;
            }
            else
            {
                loc.col++;
            }
        }
        
        data.remove_prefix( ptr );
        ptr = 0;
    }
};
