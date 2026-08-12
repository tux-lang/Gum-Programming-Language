#include "Runner.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdlib.h>
#endif

#include "Error.h"
#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"

namespace sek {

namespace {

bool isAbsolutePath(const std::string& path) {
    if (path.size() >= 2 && path[1] == ':') {
        return true;
    }

    return !path.empty() && (path[0] == '\\' || path[0] == '/');
}

std::string directoryOf(const std::string& path) {
    const std::string::size_type slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return ".";
    }

    return path.substr(0, slash);
}

std::string joinPath(const std::string& directory, const std::string& child) {
    if (directory.empty() || directory == ".") {
        return child;
    }

    const char last = directory[directory.size() - 1];
    if (last == '\\' || last == '/') {
        return directory + child;
    }

    return directory + "\\" + child;
}

std::string normalizePath(const std::string& path) {
#ifdef _WIN32
    DWORD requiredSize = GetFullPathNameA(path.c_str(), 0, nullptr, nullptr);
    if (requiredSize == 0) {
        return path;
    }

    std::vector<char> buffer(requiredSize);
    DWORD written = GetFullPathNameA(path.c_str(), requiredSize, buffer.data(), nullptr);
    if (written == 0 || written >= requiredSize) {
        return path;
    }

    return std::string(buffer.data(), written);
#else
    char* resolved = realpath(path.c_str(), nullptr);
    if (resolved == nullptr) {
        return path;
    }

    const std::string result(resolved);
    std::free(resolved);
    return result;
#endif
}

std::string resolveIncludePath(const std::string& fromFile, const std::string& includePath) {
    if (isAbsolutePath(includePath)) {
        return normalizePath(includePath);
    }

    return normalizePath(joinPath(directoryOf(fromFile), includePath));
}

void appendProgram(Program& target, Program source) {
    for (auto& statement : source) {
        target.push_back(std::move(statement));
    }
}

}  // namespace

int Runner::runFile(const std::string& filePath) const {
    std::vector<std::string> includeStack;
    Program program = loadProgram(normalizePath(filePath), includeStack);

    Interpreter interpreter;
    interpreter.execute(program);
    return 0;
}

Program Runner::loadProgram(const std::string& filePath, std::vector<std::string>& includeStack) const {
    const std::string normalizedPath = normalizePath(filePath);
    if (std::find(includeStack.begin(), includeStack.end(), normalizedPath) != includeStack.end()) {
        throw SekError("Cyclic use detected for file: " + normalizedPath);
    }

    includeStack.push_back(normalizedPath);
    Program parsed = parseFile(normalizedPath);
    Program expanded = expandProgram(std::move(parsed), normalizedPath, includeStack);
    includeStack.pop_back();
    return expanded;
}

Program Runner::expandProgram(Program program, const std::string& filePath,
                              std::vector<std::string>& includeStack) const {
    Program expanded;

    for (auto& statement : program) {
        if (const auto* use = dynamic_cast<const UseStmt*>(statement.get())) {
            appendProgram(expanded, loadProgram(resolveIncludePath(filePath, use->path.lexeme), includeStack));
            continue;
        }

        if (auto* hotkey = dynamic_cast<HotkeyStmt*>(statement.get())) {
            hotkey->body = expandProgram(std::move(hotkey->body), filePath, includeStack);
        } else if (auto* group = dynamic_cast<GroupStmt*>(statement.get())) {
            group->body = expandProgram(std::move(group->body), filePath, includeStack);
        } else if (auto* loop = dynamic_cast<LoopStmt*>(statement.get())) {
            loop->body = expandProgram(std::move(loop->body), filePath, includeStack);
        } else if (auto* ifStatement = dynamic_cast<IfStmt*>(statement.get())) {
            ifStatement->thenBody = expandProgram(std::move(ifStatement->thenBody), filePath, includeStack);
            for (auto& elif : ifStatement->elifs) {
                elif.body = expandProgram(std::move(elif.body), filePath, includeStack);
            }
            ifStatement->elseBody = expandProgram(std::move(ifStatement->elseBody), filePath, includeStack);
        }

        expanded.push_back(std::move(statement));
    }

    return expanded;
}

Program Runner::parseFile(const std::string& filePath) const {
    std::ifstream input(filePath);
    if (!input) {
        throw SekError("Cannot open file: " + filePath);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();

    Lexer lexer(buffer.str(), filePath);
    Parser parser(lexer.scanTokens());
    return parser.parse();
}

}  // namespace sek
