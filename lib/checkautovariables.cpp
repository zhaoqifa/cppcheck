/*
 * Cppcheck - A tool for static C/C++ code analysis
 * Copyright (C) 2007-2012 Daniel Marjamäki and Cppcheck team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

//---------------------------------------------------------------------------
// Auto variables checks
//---------------------------------------------------------------------------

#include "checkautovariables.h"
#include "symboldatabase.h"

#include <list>
#include <string>

//---------------------------------------------------------------------------


// Register this check class into cppcheck by creating a static instance of it..
namespace {
    static CheckAutoVariables instance;
}


bool CheckAutoVariables::isRefArg(unsigned int varId)
{
    const Variable *var = _tokenizer->getSymbolDatabase()->getVariableFromVarId(varId);

    return(var && var->isArgument() && var->isReference());
}

bool CheckAutoVariables::isPtrArg(unsigned int varId)
{
    const Variable *var = _tokenizer->getSymbolDatabase()->getVariableFromVarId(varId);

    return(var && var->isArgument() && var->isPointer());
}

bool CheckAutoVariables::isAutoVar(unsigned int varId)
{
    const Variable *var = _tokenizer->getSymbolDatabase()->getVariableFromVarId(varId);

    if (!var || !var->isLocal() || var->isStatic())
        return false;

    if (var->isReference()) {
        // address of reference variable can be taken if the address
        // of the variable it points at is not a auto-var
        // TODO: check what the reference variable references.
        return false;
    }

    return true;
}

bool CheckAutoVariables::isAutoVarArray(unsigned int varId)
{
    const Variable *var = _tokenizer->getSymbolDatabase()->getVariableFromVarId(varId);

    return (var && var->isLocal() && !var->isStatic() && var->isArray());
}

// Verification that we really take the address of a local variable
static bool checkRvalueExpression(const Variable* var, const Token* next)
{
    return((next->str() != "." || (!var->isPointer() && (!var->isClass() || var->type()))) && next->strAt(2) != ".");
}

void CheckAutoVariables::autoVariables()
{
    const SymbolDatabase *symbolDatabase = _tokenizer->getSymbolDatabase();

    std::list<Scope>::const_iterator scope;

    for (scope = symbolDatabase->scopeList.begin(); scope != symbolDatabase->scopeList.end(); ++scope) {
        // only check functions
        if (scope->type != Scope::eFunction)
            continue;

        for (const Token *tok = scope->classStart; tok && tok != scope->classEnd; tok = tok->next()) {
            // Critical assignment
            if (Token::Match(tok, "[;{}] %var% = & %var%") && isRefArg(tok->next()->varId()) && isAutoVar(tok->tokAt(4)->varId())) {
                const Variable * var = symbolDatabase->getVariableFromVarId(tok->tokAt(4)->varId());
                if (checkRvalueExpression(var, tok->tokAt(5)))
                    errorAutoVariableAssignment(tok->next(), false);
            } else if (Token::Match(tok, "[;{}] * %var% = & %var%") && isPtrArg(tok->tokAt(2)->varId()) && isAutoVar(tok->tokAt(5)->varId())) {
                const Variable * var = symbolDatabase->getVariableFromVarId(tok->tokAt(5)->varId());
                if (checkRvalueExpression(var, tok->tokAt(6)))
                    errorAutoVariableAssignment(tok->next(), false);
            } else if (Token::Match(tok, "[;{}] %var% . %var% = & %var%")) {
                // TODO: check if the parameter is only changed temporarily (#2969)
                if (_settings->inconclusive) {
                    const Variable * var1 = symbolDatabase->getVariableFromVarId(tok->next()->varId());
                    if (var1 && var1->isArgument() && var1->isPointer()) {
                        const Variable * var2 = symbolDatabase->getVariableFromVarId(tok->tokAt(6)->varId());
                        if (isAutoVar(tok->tokAt(6)->varId()) && checkRvalueExpression(var2, tok->tokAt(7)))
                            errorAutoVariableAssignment(tok->next(), true);
                    }
                }
                tok = tok->tokAt(6);
            } else if (Token::Match(tok, "[;{}] %var% . %var% = %var% ;")) {
                // TODO: check if the parameter is only changed temporarily (#2969)
                if (_settings->inconclusive) {
                    const Variable * var1 = symbolDatabase->getVariableFromVarId(tok->next()->varId());
                    if (var1 && var1->isArgument() && var1->isPointer()) {
                        if (isAutoVarArray(tok->tokAt(5)->varId()))
                            errorAutoVariableAssignment(tok->next(), true);
                    }
                }
                tok = tok->tokAt(5);
            } else if (Token::Match(tok, "[;{}] * %var% = %var% ;")) {
                const Variable * var1 = symbolDatabase->getVariableFromVarId(tok->tokAt(2)->varId());
                if (var1 && var1->isArgument() && Token::Match(var1->nameToken()->tokAt(-3), "%type% * *")) {
                    if (isAutoVarArray(tok->tokAt(4)->varId()))
                        errorAutoVariableAssignment(tok->next(), false);
                }
                tok = tok->tokAt(4);
            } else if (Token::Match(tok, "[;{}] %var% [") && Token::Match(tok->linkAt(2), "] = & %var%") && isPtrArg(tok->next()->varId()) && isAutoVar(tok->linkAt(2)->tokAt(3)->varId())) {
                const Token* const varTok = tok->linkAt(2)->tokAt(3);
                const Variable * var = symbolDatabase->getVariableFromVarId(varTok->varId());
                if (checkRvalueExpression(var, varTok->next()))
                    errorAutoVariableAssignment(tok->next(), false);
            }
            // Critical return
            else if (Token::Match(tok, "return & %var% ;") && isAutoVar(tok->tokAt(2)->varId())) {
                errorReturnAddressToAutoVariable(tok);
            } else if (Token::Match(tok, "return & %var% [") &&
                       Token::simpleMatch(tok->linkAt(3), "] ;") &&
                       isAutoVarArray(tok->tokAt(2)->varId())) {
                errorReturnAddressToAutoVariable(tok);
            } else if (Token::Match(tok, "return & %var% ;") && tok->tokAt(2)->varId()) {
                const Variable * var1 = symbolDatabase->getVariableFromVarId(tok->tokAt(2)->varId());
                if (var1 && var1->isArgument() && var1->typeEndToken()->str() != "&")
                    errorReturnAddressOfFunctionParameter(tok, tok->strAt(2));
            }
            // Invalid pointer deallocation
            else if (Token::Match(tok, "free ( %var% ) ;") || Token::Match(tok, "delete [| ]| (| %var% !![")) {
                tok = Token::findmatch(tok->next(), "%var%");
                if (isAutoVarArray(tok->varId()))
                    errorInvalidDeallocation(tok);
            }
        }
    }
}

//---------------------------------------------------------------------------

void CheckAutoVariables::returnPointerToLocalArray()
{
    const SymbolDatabase *symbolDatabase = _tokenizer->getSymbolDatabase();

    std::list<Scope>::const_iterator scope;

    for (scope = symbolDatabase->scopeList.begin(); scope != symbolDatabase->scopeList.end(); ++scope) {
        // only check functions
        if (scope->type != Scope::eFunction || !scope->function)
            continue;

        const Token *tok = scope->function->tokenDef;

        // have we reached a function that returns a pointer
        if (tok->previous() && tok->previous()->str() == "*") {
            for (const Token *tok2 = scope->classStart; tok2 && tok2 != scope->classEnd; tok2 = tok2->next()) {
                // Return pointer to local array variable..
                if (Token::Match(tok2, "return %var% ;")) {
                    const unsigned int varid = tok2->next()->varId();
                    if (isAutoVarArray(varid)) {
                        errorReturnPointerToLocalArray(tok2);
                    }
                }
            }
        }
    }
}

void CheckAutoVariables::errorReturnAddressToAutoVariable(const Token *tok)
{
    reportError(tok, Severity::error, "returnAddressOfAutoVariable", "Address of an auto-variable returned.");
}

void CheckAutoVariables::errorReturnPointerToLocalArray(const Token *tok)
{
    reportError(tok, Severity::error, "returnLocalVariable", "Pointer to local array variable returned.");
}

void CheckAutoVariables::errorAutoVariableAssignment(const Token *tok, bool inconclusive)
{
    if (!inconclusive) {
        reportError(tok, Severity::error, "autoVariables",
                    "Address of local auto-variable assigned to a function parameter.\n"
                    "Dangerous assignment - the function parameter is assigned the address of a local "
                    "auto-variable. Local auto-variables are reserved from the stack which "
                    "is freed when the function ends. So the pointer to a local variable "
                    "is invalid after the function ends.");
    } else {
        reportError(tok, Severity::error, "autoVariables",
                    "Address of local auto-variable assigned to a function parameter.\n"
                    "Function parameter is assigned the address of a local auto-variable. "
                    "Local auto-variables are reserved from the stack which is freed when "
                    "the function ends. The address is invalid after the function ends and it "
                    "might 'leak' from the function through the parameter.", true);
    }
}

void CheckAutoVariables::errorReturnAddressOfFunctionParameter(const Token *tok, const std::string &varname)
{
    reportError(tok, Severity::error, "returnAddressOfFunctionParameter",
                "Address of function parameter '" + varname + "' returned.\n"
                "Address of the function parameter '" + varname + "' becomes invalid after the function exits because "
                "function parameters are stored on the stack which is freed when the function exits. Thus the returned "
                "value is invalid.");
}

//---------------------------------------------------------------------------

// return temporary?
bool CheckAutoVariables::returnTemporary(const Token *tok) const
{
    if (!Token::Match(tok, "return %var% ("))
        return false;
    // TODO: Find all functions that return objects by value
    return bool(NULL != Token::findmatch(_tokenizer->tokens(), ("std :: string " + tok->next()->str() + " (").c_str()));
}

//---------------------------------------------------------------------------

void CheckAutoVariables::returnReference()
{
    const SymbolDatabase *symbolDatabase = _tokenizer->getSymbolDatabase();

    std::list<Scope>::const_iterator scope;

    for (scope = symbolDatabase->scopeList.begin(); scope != symbolDatabase->scopeList.end(); ++scope) {
        // only check functions
        if (scope->type != Scope::eFunction || !scope->function)
            continue;

        const Token *tok = scope->function->tokenDef;

        // have we reached a function that returns a reference?
        if (tok->previous() && tok->previous()->str() == "&") {
            for (const Token *tok2 = scope->classStart; tok2 && tok2 != scope->classEnd; tok2 = tok2->next()) {
                // return..
                if (Token::Match(tok2, "return %var% ;")) {
                    // is the returned variable a local variable?
                    const unsigned int varid1 = tok2->next()->varId();
                    const Variable *var1 = symbolDatabase->getVariableFromVarId(varid1);

                    if (isAutoVar(varid1)) {
                        // If reference variable is used, check what it references
                        if (Token::Match(var1->nameToken(), "%var% [=(]")) {
                            const Token *tok3 = var1->nameToken()->tokAt(2);
                            if (!Token::Match(tok3, "%var% [);.]"))
                                continue;

                            // Only report error if variable that is referenced is
                            // a auto variable
                            if (!isAutoVar(tok3->varId()))
                                continue;
                        }

                        // report error..
                        errorReturnReference(tok2);
                    }
                }

                // return reference to temporary..
                else if (returnTemporary(tok2)) {
                    // report error..
                    errorReturnTempReference(tok2);
                }
            }
        }
    }
}

void CheckAutoVariables::errorReturnReference(const Token *tok)
{
    reportError(tok, Severity::error, "returnReference", "Reference to auto variable returned.");
}

void CheckAutoVariables::errorReturnTempReference(const Token *tok)
{
    reportError(tok, Severity::error, "returnTempReference", "Reference to temporary returned.");
}

void CheckAutoVariables::errorInvalidDeallocation(const Token *tok)
{
    reportError(tok,
                Severity::error,
                "autovarInvalidDeallocation",
                "Deallocation of an auto-variable results in undefined behaviour.\n"
                "The deallocation of an auto-variable results in undefined behaviour. You should only free memory "
                "that has been allocated dynamically.");
}


//---------------------------------------------------------------------------

// Return c_str
void CheckAutoVariables::returncstr()
{
    // TODO: Move this to CheckStl::string_c_str

    // locate function that returns a const char *..
    const SymbolDatabase *symbolDatabase = _tokenizer->getSymbolDatabase();

    std::list<Scope>::const_iterator scope;

    for (scope = symbolDatabase->scopeList.begin(); scope != symbolDatabase->scopeList.end(); ++scope) {
        // only check functions
        if (scope->type != Scope::eFunction || !scope->function)
            continue;

        const Token *tok = scope->function->tokenDef;

        // have we reached a function that returns a const char *
        if (Token::simpleMatch(tok->tokAt(-3), "const char *")) {
            for (const Token *tok2 = scope->classStart; tok2 && tok2 != scope->classEnd; tok2 = tok2->next()) {
                // return pointer to temporary..
                if (returnTemporary(tok2)) {
                    // report error..
                    errorReturnTempPointer(tok2);
                }
            }
        }
    }
}

void CheckAutoVariables::errorReturnTempPointer(const Token *tok)
{
    reportError(tok, Severity::error, "returnTempPointer", "Pointer to temporary returned.");
}
