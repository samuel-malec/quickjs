#include "ast.h"
#include "jac/machine/compiler/bcWriter.h"
#include "jac/machine/compiler/qjsBcOpcodes.h"
#include "jac/util.h"
#include "ast2bc.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <string>


namespace jac::bc {


struct EmitState {
    BytecodeRoot& root;
    FunctionBytecode& funcBytecode;
    bool isGlobal = false;

    std::map<uint32_t, uint32_t> argMap;
    std::map<uint32_t, uint32_t> localMap;  // no differentiation - only let supported, no scope support
    int nextScopeId = 1;

    auto pushScope() {
        funcBytecode.scopeStack.emplace_back(nextScopeId++, std::get<1>(funcBytecode.scopeStack.back()));
        return Defer([this]() {
            funcBytecode.scopeStack.pop_back();
        });
    }
};


struct Lhs {
    enum class Kind {
        Local,
        Argument,
        Ident
    } kind;
    uint32_t idx;

    void get(EmitState& state) {
        switch (kind) {
            case Kind::Local:
                writeByte(state.funcBytecode.bytecode, OP_get_loc);
                writeInt<2>(state.funcBytecode.bytecode, idx);
                break;
            case Kind::Argument:
                writeByte(state.funcBytecode.bytecode, OP_get_arg);
                writeInt<2>(state.funcBytecode.bytecode, idx);
                break;
            case Kind::Ident:
                writeByte(state.funcBytecode.bytecode, OP_get_var);
                writeInt<4>(state.funcBytecode.bytecode, idx);
                break;
        }
    }

    void put(EmitState& state) {
        switch (kind) {
            case Kind::Local:
                writeByte(state.funcBytecode.bytecode, OP_put_loc);
                writeInt<2>(state.funcBytecode.bytecode, idx);
                break;
            case Kind::Argument:
                writeByte(state.funcBytecode.bytecode, OP_put_arg);
                writeInt<2>(state.funcBytecode.bytecode, idx);
                break;
            case Kind::Ident:
                writeByte(state.funcBytecode.bytecode, OP_put_var);
                writeInt<4>(state.funcBytecode.bytecode, idx);
                break;
        }
    }
};

using OptLhs = std::optional<Lhs>;


Lhs findAtom(uint32_t atomIdx, EmitState& state) {
    auto it = state.argMap.find(atomIdx);
    if (it != state.argMap.end()) {
        uint32_t argIdx = it->second;
        return Lhs{Lhs::Kind::Argument, argIdx};
    }
    it = state.localMap.find(atomIdx);
    if (it != state.localMap.end()) {
        uint32_t localIdx = it->second;
        return Lhs{Lhs::Kind::Local, localIdx};
    }

    return Lhs{Lhs::Kind::Ident, atomIdx};
}


bool emitStmt_(const ast::Statement& statement, EmitState& state);
void emitAsRV_(const ast::Expression& node, EmitState& state);
[[nodiscard]] Lhs emitAsLV(const ast::Expression& node, EmitState& state);
std::unique_ptr<FunctionBytecode> emitFnConst(const ast::Function& fun, const std::string& filename, EmitState& state);


void emitUndefined(EmitState& state) {
    writeByte(state.funcBytecode.bytecode, OP_undefined);
}

void emitConst(int32_t value, EmitState& state) {
    writeByte(state.funcBytecode.bytecode, OP_push_i32);
    writeInt<4>(state.funcBytecode.bytecode, value);
}

void emitConst(auto value, EmitState& state) {
    throw std::runtime_error("Constant of this type not supported");
}
void emitAsRV(const ast::Literal& lit, EmitState& state) {
    std::visit(overloaded{
        [&](const ast::Literal::Null&) {
            throw IRGenError("Null literals are not supported");
        },
        [&](auto value) {
            emitConst(value, state);
        }
    }, lit.value);
}

void emitAsRV(const ast::Identifier& ident, EmitState& state) {
    Lhs lhs = findAtom(state.root.addAtom(ident.name), state);
    lhs.get(state);
}

void emitAsRV(const ast::ThisExpression&, EmitState&) {
    throw std::runtime_error("ThisExpression not supported");
}

void emitAsRV(const ast::BinaryExpression& expr, EmitState& state) {
    const auto& left = *expr.left();
    const auto& right = *expr.right();

    emitAsRV_(left, state);
    emitAsRV_(right, state);

    switch (expr.op) {
        case ast::BinaryExpression::BitOr:       writeByte(state.funcBytecode.bytecode, OP_or);          break;
        case ast::BinaryExpression::BitXor:      writeByte(state.funcBytecode.bytecode, OP_xor);         break;
        case ast::BinaryExpression::BitAnd:      writeByte(state.funcBytecode.bytecode, OP_and);         break;
        case ast::BinaryExpression::Eq:          writeByte(state.funcBytecode.bytecode, OP_eq);          break;
        case ast::BinaryExpression::Neq:         writeByte(state.funcBytecode.bytecode, OP_neq);         break;
        case ast::BinaryExpression::StrictEq:    writeByte(state.funcBytecode.bytecode, OP_strict_eq);   break;
        case ast::BinaryExpression::StrictNeq:   writeByte(state.funcBytecode.bytecode, OP_strict_neq);  break;
        case ast::BinaryExpression::InstanceOf:  writeByte(state.funcBytecode.bytecode, OP_instanceof);  break;
        case ast::BinaryExpression::Lt:          writeByte(state.funcBytecode.bytecode, OP_lt);          break;
        case ast::BinaryExpression::Lte:         writeByte(state.funcBytecode.bytecode, OP_lte);         break;
        case ast::BinaryExpression::Gt:          writeByte(state.funcBytecode.bytecode, OP_gt);          break;
        case ast::BinaryExpression::Gte:         writeByte(state.funcBytecode.bytecode, OP_gte);         break;
        case ast::BinaryExpression::In:          writeByte(state.funcBytecode.bytecode, OP_in);          break;
        case ast::BinaryExpression::LShift:      writeByte(state.funcBytecode.bytecode, OP_shl);         break;
        case ast::BinaryExpression::RShift:      writeByte(state.funcBytecode.bytecode, OP_sar);         break;
        case ast::BinaryExpression::URShift:     writeByte(state.funcBytecode.bytecode, OP_shr);         break;
        case ast::BinaryExpression::Add:         writeByte(state.funcBytecode.bytecode, OP_add);         break;
        case ast::BinaryExpression::Sub:         writeByte(state.funcBytecode.bytecode, OP_sub);         break;
        case ast::BinaryExpression::Mul:         writeByte(state.funcBytecode.bytecode, OP_mul);         break;
        case ast::BinaryExpression::Div:         writeByte(state.funcBytecode.bytecode, OP_div);         break;
        case ast::BinaryExpression::Rem:         writeByte(state.funcBytecode.bytecode, OP_mod);         break;
        case ast::BinaryExpression::Exp:         writeByte(state.funcBytecode.bytecode, OP_pow);         break;
        default:
            throw std::runtime_error("Unsupported binary operator: " + std::to_string(expr.op));
    };
}

void emitAsRV(const ast::UnaryExpression& expr, EmitState& state) {
    const auto& res = *expr.expression();

    emitAsRV_(res, state);

    switch (expr.op) {

        case ast::UnaryExpression::LogNot:  writeByte(state.funcBytecode.bytecode, OP_lnot);    break;
        case ast::UnaryExpression::BitNot:  writeByte(state.funcBytecode.bytecode, OP_not);     break;
        case ast::UnaryExpression::Plus:    writeByte(state.funcBytecode.bytecode, OP_plus);    break;
        case ast::UnaryExpression::Minus:   writeByte(state.funcBytecode.bytecode, OP_neg);     break;
        case ast::UnaryExpression::Typeof:  writeByte(state.funcBytecode.bytecode, OP_typeof);  break;
        case ast::UnaryExpression::Void:    writeByte(state.funcBytecode.bytecode, OP_drop);    break;
        case ast::UnaryExpression::Delete:  writeByte(state.funcBytecode.bytecode, OP_delete);  break;
        case ast::UnaryExpression::Await:   writeByte(state.funcBytecode.bytecode, OP_await);   break;
        default:
            throw std::runtime_error("Unsupported unary operator: " + std::to_string(expr.op));
    };
}

void emitAsRV(const ast::Assignment& expr, EmitState& state) {
    auto lhs = emitAsLV(*expr.left(), state);
    emitAsRV_(*expr.right(), state);

    lhs.put(state);
}

void emitAsRV(const ast::CommaExpression& expr, EmitState& state) {
    for (size_t i = 0; i + 1 < expr.itemCount(); i++) {
        emitAsRV_(*expr.itemGet(i), state);
        // emitPop(state);
    }
    if (!expr.itemCount()) {
        emitAsRV_(*expr.itemGet(expr.itemCount() - 1), state);
    }
    throw IRGenError("Empty expression");
}

void emitAsRV(const ast::CallExpression& expr, EmitState& state) {
    emitAsRV_(*expr.callee(), state);
    const auto& args = *expr.arguments();
    for (unsigned i = 0; i < args.argCount(); i++) {
        emitAsRV_(*args.argGet(i), state);
    }
    writeByte(state.funcBytecode.bytecode, OP_call);
    writeInt<2>(state.funcBytecode.bytecode, args.argCount());
}


[[nodiscard]] Lhs emitAsLV(const ast::Expression& node, EmitState& state) {
    using Types = TypeList<ast::Identifier, ast::MemberAccessExpression, ast::Expression>;

    return ast::visitNode<Types>(node, overloaded{
        [&](const ast::Identifier& ident) -> Lhs {
                uint32_t atom = state.root.addAtom(ident.name);
                auto lhs = findAtom(atom, state);
                return lhs;
        },
        [&](const ast::MemberAccessExpression& expr) -> Lhs {
            throw IRGenError("Member access not supported as left-hand side expression");
        },
        [&](const ast::Expression&) -> Lhs {
            throw IRGenError("Assignment target is not a valid left-hand side expression");
        }
    });
}


void emitAsRV_(const ast::Expression& node, EmitState& state) {
    ast::visitNode<jac::TypeList<
        jac::ast::Identifier,
        jac::ast::Literal,
        jac::ast::BinaryExpression,
        // jac::ast::ConditionalExpression,
        jac::ast::UnaryExpression,
        // jac::ast::UpdateExpression,
        // jac::ast::Function,
        // jac::ast::NewCallExpression,
        jac::ast::CommaExpression,
        jac::ast::Assignment,
        // jac::ast::MemberAccessExpression,
        // jac::ast::TaggedTemplateExpression,
        jac::ast::CallExpression,
        jac::ast::ThisExpression
    >>(node, overloaded{
        [&](const auto& expr) {
            emitAsRV(expr, state);
        }
    });
}

bool emitStmt(const ast::EmptyStatement&, EmitState&) {
    return false;
}

bool emitStmt(const ast::ExpressionStatement& stmt, EmitState& state) {
    emitAsRV_(*stmt.expression(), state);
    // emitPop();
    return false;
}

bool emitStmt(const ast::IfStatement&, EmitState& state) {
    throw std::runtime_error("IfStatement not supported");
}

bool emitStmt(const ast::ContinueStatement&, EmitState& state) {
    throw std::runtime_error("ContinueStatement not supported");
}

bool emitStmt(const ast::BreakStatement&, EmitState& state) {
    throw std::runtime_error("BreakStatement not supported");
}

bool emitStmt(const ast::ReturnStatement& ret, EmitState& state) {
    if (ret.expression()) {
        emitAsRV_(*ret.expression(), state);
    } else {
        emitUndefined(state);
    }
    writeByte(state.funcBytecode.bytecode, OP_return);
    return true;
}

bool emitStmt(const ast::ThrowStatement&, EmitState& state) {
    throw std::runtime_error("ThrowStatement not supported");
}

bool emitStmt(const ast::DebuggerStatement&, EmitState& state) {
    throw std::runtime_error("DebuggerStatement not supported");
}

bool emitStmt(const ast::LexicalDeclaration& decl, EmitState& state) {
    if (state.isGlobal) {
        throw std::runtime_error("Global lexical declarations not supported");
    }
    if (decl.isConst) {
        throw std::runtime_error("const declarations not supported");
    }
    for (unsigned i = 0; i < decl.bindingCount(); i++) {
        const auto& binding = decl.bindingGet(i);
        if (!binding->target()) {
            throw std::runtime_error("Unsupported lexical binding without target");
        }

        uint32_t atom = state.root.addAtom(binding->target()->name);
        uint32_t localIdx = state.funcBytecode.addLocal(atom, 0);
        state.localMap[atom] = localIdx;
        writeByte(state.funcBytecode.bytecode, OP_set_loc_uninitialized);
        writeInt<2>(state.funcBytecode.bytecode, localIdx);

        if (binding->initializer()) {
            emitAsRV_(*binding->initializer(), state);
        }
        else {
            emitUndefined(state);
        }
        Lhs lhs{Lhs::Kind::Local, localIdx};
        lhs.put(state);
    }
    return false;
}


bool emitStmt(const ast::StatementList& list, EmitState& state) {
    auto _ = state.pushScope();
    for (size_t i = 0; i < list.statementCount(); i++) {
        auto stmt = list.statementGet(i);
        if (emitStmt_(*stmt, state)) {
            return true;
        }
    }

    return false;
}

bool emitStmt(const ast::HoistableDeclaration& decl, EmitState& state) {
    auto& fun = *decl.function();
    state.funcBytecode.cpool.emplace_back(emitFnConst(fun, "arst", state));

    uint32_t atom = state.root.addAtom(fun.name()->name);

    writeByte(state.funcBytecode.bytecode, OP_fclosure);
    writeInt<4>(state.funcBytecode.bytecode, static_cast<uint32_t>(state.funcBytecode.cpool.size() - 1));

    if (state.isGlobal) {
        writeByte(state.funcBytecode.intro, OP_check_define_var);
        writeInt<4>(state.funcBytecode.intro, atom);
        writeByte(state.funcBytecode.intro, 64);  // flags

        writeByte(state.funcBytecode.bytecode, OP_define_func);
        writeInt<4>(state.funcBytecode.bytecode, atom);
        writeByte(state.funcBytecode.bytecode, 0);  // flags
    }
    else {
        uint32_t localIdx = state.funcBytecode.addLocal(atom, 3);
        state.localMap[atom] = localIdx;

        Lhs lhs{Lhs::Kind::Local, localIdx};
        lhs.put(state);
    }

    return false;
}



bool emitStmt_(const ast::Statement& statement, EmitState& state) {
    return ast::visitNode<jac::TypeList<
        jac::ast::ExpressionStatement,
        jac::ast::StatementList,
        jac::ast::LexicalDeclaration,
        // jac::ast::IterationStatement,
        jac::ast::ContinueStatement,
        jac::ast::BreakStatement,
        jac::ast::ReturnStatement,
        jac::ast::ThrowStatement,
        jac::ast::DebuggerStatement,
        jac::ast::HoistableDeclaration,
        jac::ast::IfStatement
    >>(statement, overloaded{
        [&](const auto& expr) -> bool {
            return emitStmt(expr, state);
        }
    });
}


std::unique_ptr<FunctionBytecode> emitFnConst(const ast::Function& fun, const std::string& filename, EmitState& state) {
    auto& root = state.root;
    std::unique_ptr<FunctionBytecode> funcBytecode = std::make_unique<FunctionBytecode>(
        root.addAtom(fun.name()->name),
        root.addAtom(filename)
    );

    EmitState nestState{root, *funcBytecode};

    for (unsigned i = 0; i < fun.parameters()->parameterCount(); i++) {
        const auto& param = *fun.parameters()->parameterGet(i);
        uint32_t argIdx = funcBytecode->argCount++;
        auto atom = root.addAtom(param.target()->name);
        nestState.argMap[atom] = argIdx;

        funcBytecode->args.push_back(atom);
    }

    emitStmt(*fun.body(), nestState);

    return funcBytecode;
}


BytecodeRoot emit(const ast::Script& fun, const std::string& filename) {
    BytecodeRoot root;
    root.version = 4;

    std::unique_ptr<FunctionBytecode> funcBytecode = std::make_unique<FunctionBytecode>(
        root.addAtom("<script>"),
        root.addAtom(filename)
    );

    EmitState state{root, *funcBytecode, true};

    emitStmt(*fun.body(), state);
    writeByte(state.funcBytecode.bytecode, OP_return);

    root.child = std::move(funcBytecode);
    return root;
}


}  // namespace jac::bc
