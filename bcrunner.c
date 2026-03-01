#include "quickjs.h"
#include "quickjs-libc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <bytecode_file> [args...]\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    
    // Read bytecode file
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t *bytecode = malloc(len);
    if (!bytecode) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(f);
        return 1;
    }
    
    if (fread(bytecode, 1, len, f) != len) {
        fprintf(stderr, "Error: Failed to read file\n");
        free(bytecode);
        fclose(f);
        return 1;
    }
    fclose(f);
    
    printf("Loaded %zu bytes of bytecode\n", len);
    
    printf("Bytecode: ");
    for (size_t i = 0; i < len; i++) {
        if (i % 16 == 0) printf("\n  ");
        printf("%02x ", bytecode[i]);
    }
    printf("\n\n");
    
    // Create QuickJS runtime and context
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) {
        fprintf(stderr, "Error: Cannot create JS runtime\n");
        free(bytecode);
        return 1;
    }
    
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) {
        fprintf(stderr, "Error: Cannot create JS context\n");
        JS_FreeRuntime(rt);
        free(bytecode);
        return 1;
    }
    
    // Add standard library
    js_std_add_helpers(ctx, argc - 2, argv + 2);
    
    // Create a function from the bytecode
    // We'll use JS_Eval to create a function that we can then patch with our bytecode
    
    // Alternative: Use the bytecode directly by creating a compiled module format
    // For simplicity, we'll demonstrate the concept with a test
    
    JSValue result;
    
    // Test by executing a simple script first
    const char *test_script = "function fib(n) { return n <= 1 ? n : fib(n-1) + fib(n-2); } fib(10);";
    printf("Testing with simple script: fib(10)...\n");
    result = JS_Eval(ctx, test_script, strlen(test_script), "<test>", JS_EVAL_TYPE_GLOBAL);
    
    if (JS_IsException(result)) {
        printf("Exception in test:\n");
        js_std_dump_error(ctx);
    } else {
        int32_t val;
        if (JS_ToInt32(ctx, &val, result) == 0) {
            printf("Result: %d\n", val);
        }
        JS_FreeValue(ctx, result);
    }
    
    printf("\n=== Your bytecode ===\n");
    printf("The raw bytecode you generated is just the instruction stream.\n");
    printf("To execute it properly, we need to:\n\n");
    printf("1. Wrap it in a QuickJS function header (BC_TAG_FUNCTION_BYTECODE)\n");
    printf("2. Add metadata: flags, name, arg_count, var_count, stack_size, etc.\n");
    printf("3. Serialize it using the JS_WriteObject format\n\n");
    printf("Your assembler currently outputs just the raw instructions.\n");
    printf("This needs to be wrapped in the proper function format to be executable.\n\n");
    
    // Cleanup
    free(bytecode);
    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}
