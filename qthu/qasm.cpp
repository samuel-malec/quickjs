#include "lexer.hpp"
#include "opcodes.hpp"
#include "encoder.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <ios>
#include <sstream>
#include <string>
#include <cstdint>
#include <map>
#include <cctype>
#include <cstring>
#include <memory>

struct Config 
{
    std::string file_in;
    std::string file_out;
};

struct FunctionMetadata
{
    std::string name = "main";
    uint16_t arg_count = 0;
    uint16_t var_count = 0;
    uint16_t stack_size = 4;
    bool strict_mode = true;
};

struct CompiledFunction
{
    std::string name;
    FunctionMetadata meta;
    bytes bytecode;
};

FunctionMetadata parse_metadata( const std::vector< Token >& tokens, size_t& idx )
{
    FunctionMetadata meta;
    if ( idx >= tokens.size() || tokens[ idx ].cat != Token::FUNC_NAME )
        throw std::runtime_error( "Expected function name after .function "  );

    meta.name = tokens[ idx ].data;
    idx++;
    
    while ( idx < tokens.size() && tokens[ idx ].cat == Token::DIRECTIVE )
    {
        std::string dir = tokens[ idx ].data;      
        if ( dir == ".function" ) 
            throw std::runtime_error( "Function " + meta.name + " has empty body" );
        
        idx++; 
        if ( dir == ".args" || dir == ".locals" || dir == ".stack_size" )
        {
            if ( idx < tokens.size() && tokens[ idx ].cat == Token::NUMBER )
            {
                uint16_t val = std::stoi( std::string( tokens[ idx ].data ) );
                if ( dir == ".args" ) meta.arg_count = val;
                else if ( dir == ".locals" ) meta.var_count = val;
                else if ( dir == ".stack_size" ) meta.stack_size = val;
                idx++;
            }
        }
    }
    
    return meta;
}

bool no_operands( std::string format )
{
    return format == "none" || format == "none_int" || format == "none_loc" || format == "none_arg" || 
            format == "none_var_ref" || format == "npopx";  
}

bytes assemble_function( const std::vector< Token >& tokens,
                         size_t& token_idx, 
                         const FunctionMetadata& meta,
                         const std::map< std::string, OPCodeInfo >& opcode_map )
{
    bytes bytecode_body;
    std::map< std::string, size_t > labels;
    std::vector< std::tuple< size_t, std::string, std::string > > fixups;
    size_t pc = 0;
    
    while ( token_idx < tokens.size() )
    {
        const auto& token = tokens[ token_idx ];
        if ( token.cat == Token::END ) break;

        if ( token.cat == Token::LABEL_DEF )
        {
            labels[ token.data ] = pc;
            token_idx++;
            continue;
        }
       
        if ( token.cat == Token::DIRECTIVE )
        {
            if ( token.data == ".function" )
                break;
            token_idx++;
            continue;
        }
        
        if ( token.cat == Token::OPCODE )
        {
            auto it = opcode_map.find( token.data );
            if ( it == opcode_map.end() )
                throw std::runtime_error( "Unknown opcode: " + token.data + " at line " + std::to_string( token.loc.line ) );
            
            const auto& info = it->second;
            bytecode_body.push_back( info.opcode );
            pc++;
             
            if ( no_operands( info.format ) )
            {
                token_idx++;
                continue;
            }

            token_idx++;
            if ( token_idx >= tokens.size() || ( tokens[ token_idx ].cat != Token::NUMBER 
                && tokens[ token_idx ].cat != Token::LABEL ) )
                throw std::runtime_error( "Missing operand for " + token.data + " at line " 
                    + std::to_string( token.loc.line ) + ", col " + std::to_string( token.loc.col ) );
            
            const auto& operand = tokens[ token_idx ];
            if ( info.format == "i32" || info.format == "const" )
            {
                if ( operand.cat == Token::NUMBER )
                {
                    int32_t val = std::stoi( operand.data );
                    encode_i32( bytecode_body, val );
                    pc += 4;
                } 
                else
                    throw std::runtime_error( "Expected number for " + info.format );
            } 
            
            else if ( info.format == "u32" )
            {
                if ( operand.cat == Token::NUMBER )
                {
                    uint32_t val = std::stoul( operand.data );
                    encode_u32( bytecode_body, val );
                    pc += 4;
                } else
                    throw std::runtime_error( "Expected number for " + info.format );
            }

            else if ( info.format == "i16" || info.format == "u16" || 
                        info.format == "loc" || info.format == "arg" || 
                        info.format == "var_ref" || info.format == "npop_u16" ||
                        info.format == "npop" )
            {
                if ( operand.cat == Token::NUMBER )
                {
                    int16_t val = std::stoi( operand.data );
                    encode_i16( bytecode_body, val );
                    pc += 2;
                } 
                else
                    throw std::runtime_error( "Expected number for " + info.format );
            } 
            
            else if ( info.format == "i8" || info.format == "u8" || 
                        info.format == "loc8" || info.format == "const8" )
            {
                if ( operand.cat == Token::NUMBER )
                {
                    int8_t val = std::stoi( operand.data );
                    encode_i8( bytecode_body, val );
                    pc += 1;
                } else
                    throw std::runtime_error( "Expected number for " + info.format );
            }
            
            else if ( info.format == "label" || info.format == "label16" || info.format == "label8" )
            {
                size_t fixup_offset = bytecode_body.size();
                if ( info.format == "label" )
                {
                    encode_i32( bytecode_body, 0 );
                    pc += 4;
                } 
                else if ( info.format == "label16" )
                {
                    encode_i16( bytecode_body, 0 );
                    pc += 2;
                } 
                else
                {
                    encode_i8( bytecode_body, 0 );
                    pc += 1;
                }
                
                if ( operand.cat == Token::LABEL ) 
                    fixups.push_back( { fixup_offset, operand.data, info.format } );
                else 
                    throw std::runtime_error( "Expected label for " + info.format );
            }
        }
            
        token_idx++;
    }
    
    for ( const auto& [ offset, label, format ] : fixups )
    {
        auto it = labels.find( label );
        if ( it == labels.end() )
            throw std::runtime_error( "Undefined label: " + label );
        
        size_t target = it->second;
        if ( format == "label" )
        {
            int32_t rel = target - offset;
            *reinterpret_cast< int32_t* >( &bytecode_body[ offset ] ) = rel;
        } 
        else if ( format == "label16" )
        {
            int16_t rel = target - offset;
            *reinterpret_cast< int16_t* >( &bytecode_body[ offset ] ) = rel;
        }
        else if ( format == "label8" )
        {
            int8_t rel = target - offset;
            bytecode_body[ offset ] = static_cast< uint8_t >( rel );
        }
    }
    
    return bytecode_body;
}

bytes wrap_function_bytecode( const bytes& bytecode_body, 
                              const FunctionMetadata& meta,
                              const std::vector< CompiledFunction >& cpool ) 
{
    bytes result;
    result.push_back( 0x05 ); 
    
    std::vector< std::string > atoms;
    atoms.push_back( meta.name );
    for ( const auto& func : cpool )
        atoms.push_back( func.name );
    
    encode_leb128_u( result, atoms.size() );
    for ( const auto& atom : atoms )
        encode_string( result, atom );
    
    result.push_back( 0x0c ); // BC_TAG_FUNCTION_BYTECODE
    
    // function header
    uint16_t flags = 0x0001;
    encode_u16( result, flags );
    encode_u8( result, 1 ); // jsMode
    
    encode_atom( result, 0 );
    encode_leb128_u( result, meta.arg_count );
    encode_leb128_u( result, meta.var_count );
    encode_leb128_u( result, meta.arg_count ); 
    encode_leb128_u( result, meta.stack_size );
    encode_leb128_u( result, 0 ); 
    encode_leb128_i( result, 0 ); 
    encode_leb128_i( result, static_cast<int32_t>(cpool.size()) ); 
    encode_leb128_i( result, static_cast<int32_t>(bytecode_body.size()) );
    encode_leb128_i( result, meta.var_count + meta.arg_count ); 
    
    for ( uint32_t i = 0; i < meta.arg_count; i++ ) 
    {
        encode_atom( result, 0 );
        encode_leb128_i( result, 0 );
        encode_leb128_u( result, 0 );
        encode_u8( result, 0 );
    }
    
    for ( uint32_t i = 0; i < meta.var_count; i++)
    {
        encode_atom( result, 0 );
        encode_leb128_i( result, 0 );
        encode_leb128_u( result, 0 );
        encode_u8( result, 0 );
    }
    
    // function body
    result.insert( result.end(), bytecode_body.begin(), bytecode_body.end() );
    
    for ( size_t cpool_idx = 0; cpool_idx < cpool.size(); cpool_idx++ )
    {
        const auto& func = cpool[ cpool_idx ];
        
        result.push_back( 0x0c );
        uint16_t nested_flags = 0x0001;
        encode_u16( result, nested_flags );
        encode_u8( result, 1 );
        
        encode_atom( result, cpool_idx + 1 ); // atom index (offset by 1 for main function)
        encode_leb128_u( result, func.meta.arg_count );
        encode_leb128_u( result, func.meta.var_count );
        encode_leb128_u( result, func.meta.arg_count );
        encode_leb128_u( result, func.meta.stack_size );
        encode_leb128_u( result, 0 );
        encode_leb128_i( result, 0 );
        encode_leb128_i( result, 0 );
        encode_leb128_i( result, static_cast<int32_t>(func.bytecode.size()) );
        encode_leb128_i( result, func.meta.var_count + func.meta.arg_count );
        
        // args
        for ( uint32_t i = 0; i < func.meta.arg_count; i++ )
        {
            encode_atom( result, 0 );
            encode_leb128_i( result, 0 );
            encode_leb128_u( result, 0 );
            encode_u8( result, 0 );
        }
        
        // locals
        for ( uint32_t i = 0; i < func.meta.var_count; i++)
        {
            encode_atom( result, 0 );
            encode_leb128_i( result, 0 );
            encode_leb128_u( result, 0 );
            encode_u8( result, 0 );
        }
        
        result.insert(result.end(), func.bytecode.begin(), func.bytecode.end());
    }
    
    return result;
}

bytes assemble_file( const std::string& content ) 
{
    Lexer lexer{ content };
    auto tokens = lexer.lex();
    std::vector< CompiledFunction > functions;
    size_t token_idx = 0;
    
    while ( token_idx < tokens.size() )
    {
        if ( tokens[ token_idx ].cat == Token::END ) break;
        
        if ( tokens[ token_idx ].cat == Token::DIRECTIVE && tokens[ token_idx ].data == ".function" )
        {
            token_idx++; 
            FunctionMetadata meta = parse_metadata( tokens, token_idx );
            bytes body = assemble_function( tokens, token_idx, meta, get_opcode_map() );
            functions.push_back( { meta.name, meta, body } );
        }
        else token_idx++;
    }
    
    if ( functions.empty() )
        throw std::runtime_error("No functions found in input");
    
    // First function is main, rest are in constant pool
    FunctionMetadata main_meta = functions[ 0 ].meta;
    bytes main_bytecode = functions[ 0 ].bytecode;
    
    std::vector< CompiledFunction > cpool;
    for ( size_t i = 1; i < functions.size(); i++ )
        cpool.push_back( functions[ i ] );
    
    return wrap_function_bytecode( main_bytecode, main_meta, cpool );
}

Config parse_config( int argc, char* argv[] ) 
{
    std::string in = argv[ 1 ];
    if ( argc == 2 )
        return Config{ argv[ 1 ], std::string( "out.qbc" ) };
    
    return Config{ argv[ 1 ], argv[ 3 ] };
} 

std::string read_file( std::ifstream& file )
{
    std::stringstream sstream;
    sstream << file.rdbuf();
    return sstream.str();
}

int main( int argc, char* argv[] )
{
    if ( argc != 2 && argc != 4 )
    {
        std::cout << "Usage: qjsasm_multi in_file.txt [-o out_file.qbc]\n";
        return -1;
    }

    if ( argc == 4 && std::string( argv[ 2 ] ) != "-o" )
    {
        std::cout << "Wrong flag: " << argv[ 2 ] << ", use -o to specify output \n";
        return -1;
    }

    Config config = parse_config( argc, argv );
    std::ifstream file( config.file_in, std::ios::in );
    if ( !file.is_open() )
    {
        std::cout << "Couldn't open " << config.file_in << " \n";
        return -1;
    }

    std::string content = read_file( file );
    try
    {
        bytes bytecode = assemble_file( content );
        if ( bytecode.empty() )
        {
            std::cout << "No bytecode generated\n";
            return -1;
        }

        std::ofstream out( config.file_out, std::ios::out | std::ios::binary );
        if ( !out.is_open() )
        {
            std::cout << "Couldn't open output file " << config.file_out << '\n';
            return -1;
        }

        out.write( reinterpret_cast< const char* >( bytecode.data() ), bytecode.size() );
        std::cout << "Successfully converted " << content.size() << " bytes of text to "
                  << bytecode.size() << " bytes of bytecode\n";
        std::cout << "Output written to: " << config.file_out << "\n";
        
    } catch ( const std::exception& e )
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
