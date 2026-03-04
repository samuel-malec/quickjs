#include "quickjs.h"
#include "quickjs-libc.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <bytecode_file>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return 1;
    }
    
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    uint8_t *buf = malloc(size);
    if (!buf) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(file);
        return 1;
    }
    
    if (fread(buf, 1, size, file) != size) {
        fprintf(stderr, "Error: Failed to read file\n");
        free(buf);
        fclose(file);
        return 1;
    }
    fclose(file);
    
    printf("Loaded %zu bytes from %s\n", size, filename);
    printf("Bytecode: ");
    for ( size_t i = 0; i < size; ++i )
    {
        if ( i % 16 == 0 ) printf("\n ");
        printf("%02x ", buf[ i ]);
    }
    printf("\n=========================\n");

    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext( rt );

    if ( !rt || !ctx )
    {
        fprintf( stderr, "QuickJS init failed\n" );
        free(buf);
        return 1;
    }

    js_std_init_handlers( rt );
    JS_SetModuleLoaderFunc( rt, NULL, js_module_loader, NULL );
    js_std_add_helpers( ctx, 0, NULL );

    JSValue obj = JS_ReadObject( ctx, buf, size, JS_READ_OBJ_BYTECODE );
    
    if ( JS_IsException( obj ) )
    {
        fprintf( stderr, "Failed to read bytecode\n" );
        js_std_dump_error( ctx );
        free(buf);
        JS_FreeContext( ctx );
        JS_FreeRuntime( rt );
        return 1;
    }

    // The loaded bytecode must be evaluated to execute as a module/script
    // For functions with arguments, we need to wrap it or modify the format
    // For now, this will call it with undefined arguments
    JSValue val = JS_EvalFunction( ctx, obj );
    
    if ( JS_IsException( val ) )
    {
        fprintf( stderr, "Runtime exception:\n" );
        js_std_dump_error( ctx );
        free(buf);
        JS_FreeContext( ctx );
        JS_FreeRuntime( rt );
        return 1;
    }

    JSValue str_val = JS_ToString(ctx, val);
    if (!JS_IsException(str_val)) {
        const char *str = JS_ToCString(ctx, str_val);
        if (str) {
            printf("Returned value: %s\n", str);
            JS_FreeCString(ctx, str);
        }
        JS_FreeValue(ctx, str_val);
    }

    JS_FreeValue( ctx, val );
    js_std_free_handlers( rt );
    JS_FreeContext( ctx );
    JS_FreeRuntime( rt );
    free( buf );
    return 0;
}