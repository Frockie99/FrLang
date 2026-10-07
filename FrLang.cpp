#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cctype>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <optional>
#include <cmath>
using namespace std;

ifstream inputFile("input.txt");

template <typename T>
void output(const T& value) {
    cout << value << '\n';
}

struct Variable {
    string type;
    string value;
};

struct UserFunction {
    vector<string> params;
    string body;  // ends with ';'
};

unordered_map<string, Variable> variables;
unordered_map<string, UserFunction> user_functions;

// --- forward declarations ------------------------------------------------

void run_source(istream& in);
void execute(const string& fn, const vector<string>& args);

// --- helpers -------------------------------------------------------------

string trim(const string& s) {
    size_t start = 0;
    size_t end = s.size();
    while (start < end && isspace(static_cast<unsigned char>(s[start]))) start++;
    while (end > start && isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

vector<string> split_args(const string& s) {
    vector<string> result;
    if (s.empty()) return result;

    string current;
    bool in_quotes = false;
    int depth = 0;
    for (char c : s) {
        if (c == '"') {
            in_quotes = !in_quotes;
            current += c;
        } else if (c == '(') {
            depth++;
            current += c;
        } else if (c == ')') {
            depth--;
            current += c;
        } else if (c == ',' && !in_quotes && depth == 0) {
            result.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    result.push_back(trim(current));
    return result;
}

string resolve(const string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    auto it = variables.find(s);
    if (it != variables.end()) return it->second.value;
    return s;
}

bool try_parse_double(const string& s, double& out) {
    try {
        size_t pos = 0;
        out = stod(s, &pos);
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

string format_number(double r) {
    if (r == static_cast<long long>(r)) {
        return to_string(static_cast<long long>(r));
    }
    string s = to_string(r);
    s.erase(s.find_last_not_of('0') + 1, string::npos);
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

string infer_type(const string& value) {
    double dummy;
    if (try_parse_double(value, dummy)) return "number";
    return "string";
}

bool is_builtin(const string& name) {
    return name == "output" || name == "variable" || name == "math"
        || name == "if" || name == "define";
}

bool evaluate_comparison(const string& lhs_raw,
                         const string& op,
                         const string& rhs_raw) {
    const string lhs = resolve(lhs_raw);
    const string rhs = resolve(rhs_raw);

    double a = 0, b = 0;
    if (!try_parse_double(lhs, a) || !try_parse_double(rhs, b)) {
        throw runtime_error("comparison: operands must be numeric");
    }

    if      (op == "==") return a == b;
    else if (op == "!=") return a != b;
    else if (op == "<")  return a <  b;
    else if (op == ">")  return a >  b;
    else if (op == "<=") return a <= b;
    else if (op == ">=") return a >= b;
    else throw runtime_error("comparison: unknown operator '" + op + "'");
}

// --- execution -----------------------------------------------------------

void execute(const string& fn, const vector<string>& args) {
    // User-defined functions are dispatched here, before builtins,
    // but only if the name is not a protected builtin.
    if (!is_builtin(fn)) {
        auto uf = user_functions.find(fn);
        if (uf != user_functions.end()) {
            const UserFunction& def = uf->second;
            if (args.size() != def.params.size()) {
                throw runtime_error("function '" + fn + "' expects " +
                                    to_string(def.params.size()) +
                                    " arguments, got " + to_string(args.size()));
            }

            // Save any prior bindings of the parameter names, then overwrite.
            vector<optional<Variable>> saved(def.params.size());
            for (size_t i = 0; i < def.params.size(); ++i) {
                auto it = variables.find(def.params[i]);
                if (it != variables.end()) saved[i] = it->second;
                variables[def.params[i]] = { "value", resolve(args[i]) };
            }

            // Execute the body as its own little program.
            istringstream iss(def.body);
            run_source(iss);

            // Restore the caller's bindings.
            for (size_t i = 0; i < def.params.size(); ++i) {
                if (saved[i].has_value()) variables[def.params[i]] = *saved[i];
                else variables.erase(def.params[i]);
            }
            return;
        }
    }

    if (fn == "output") {
        if (args.size() != 1) {
            throw runtime_error("output expects 1 argument, got " +
                                to_string(args.size()));
        }
        output(resolve(args[0]));

    } else if (fn == "variable") {
        string type, name, raw_value;

        if (args.size() == 2) {
            name = args[0];
            raw_value = args[1];
        } else if (args.size() == 3) {
            type = args[0];
            name = args[1];
            raw_value = args[2];
        } else {
            throw runtime_error("variable expects 2 or 3 arguments, got " +
                                to_string(args.size()));
        }

        const string value = resolve(raw_value);

        if (name.empty()) {
            throw runtime_error("variable name cannot be empty");
        }
        if (type.empty()) {
            type = infer_type(value);
        }

        variables[name] = { type, value };

    } else if (fn == "math") {
        if (args.size() != 3) {
            throw runtime_error("math expects 3 arguments, got " +
                                to_string(args.size()));
        }

        const string lhs = resolve(args[0]);
        const string op  = args[1];
        const string rhs = resolve(args[2]);

        double a = 0, b = 0;
        const bool a_num = try_parse_double(lhs, a);
        const bool b_num = try_parse_double(rhs, b);
        const bool both_numeric = a_num && b_num;

        string result_value;

        if (!both_numeric) {
            if (op == "+") {
                result_value = lhs + rhs;
            } else {
                throw runtime_error("math: non-numeric operands for operator '" + op + "'");
            }
        } else {
            double r = 0;
            if      (op == "+")  r = a + b;
            else if (op == "-")  r = a - b;
            else if (op == "*")  r = a * b;
            else if (op == "/") {
                if (b == 0) throw runtime_error("math: division by zero");
                r = a / b;
            }
            else if (op == "%") {
                if (b == 0) throw runtime_error("math: modulo by zero");
                r = fmod(a, b);
            }
            else if (op == "^")  r = pow(a, b);
            else if (op == "==") r = (a == b) ? 1 : 0;
            else if (op == "!=") r = (a != b) ? 1 : 0;
            else if (op == "<")  r = (a <  b) ? 1 : 0;
            else if (op == ">")  r = (a >  b) ? 1 : 0;
            else if (op == "<=") r = (a <= b) ? 1 : 0;
            else if (op == ">=") r = (a >= b) ? 1 : 0;
            else throw runtime_error("math: unknown operator '" + op + "'");

            result_value = format_number(r);
        }

        variables["result"] = { infer_type(result_value), result_value };

    } else if (fn == "if") {
        if (args.size() < 4) {
            throw runtime_error("if expects at least 4 arguments, got " +
                                to_string(args.size()));
        }

        const bool truthy = evaluate_comparison(args[0], args[1], args[2]);

        if (truthy) {
            const string target = args[3];
            vector<string> target_args(args.begin() + 4, args.end());
            execute(target, target_args);
        }

    } else if (fn == "define") {
        if (args.size() < 2) {
            throw runtime_error("define expects (name, [params...], (body)), got " +
                                to_string(args.size()));
        }

        string name = args[0];
        if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
            name = name.substr(1, name.size() - 2);
        }
        if (name.empty()) {
            throw runtime_error("define: function name cannot be empty");
        }
        if (is_builtin(name)) {
            throw runtime_error("define: cannot redefine builtin '" + name + "'");
        }

        string body = args.back();
        if (body.size() < 2 || body.front() != '(' || body.back() != ')') {
            throw runtime_error("define: body must be wrapped in parentheses, e.g. (statement); (statement)");
        }
        body = body.substr(1, body.size() - 2);
        body = trim(body);
        if (body.empty()) {
            throw runtime_error("define: body cannot be empty");
        }
        if (body.back() != ';') body += ';';

        vector<string> params;
        for (size_t i = 1; i + 1 < args.size(); ++i) {
            params.push_back(args[i]);
        }

        user_functions[name] = { params, body };

    } else {
        throw runtime_error("unrecognized function: " + fn);
    }
}

// --- tokenizer / parser --------------------------------------------------

enum class State {
    Word,
    ExpectingOpenParen,
    Args,
    ExpectingSemicolon,
    Comment
};

void run_source(istream& in) {
    char c;
    string word;
    string args;
    string current_function;
    State state = State::Word;
    int paren_depth = 0;

    while (in.get(c)) {
        switch (state) {

            case State::Word:
                if (isalpha(c) || c == '_' || (isdigit(c) && !word.empty())) {
                    word += c;
                } else if (isspace(c)) {
                    if (!word.empty()) {
                        throw runtime_error(
                            "function name '" + word + "' is missing '('");
                    }
                } else if (c == '(') {
                    if (word.empty()) {
                        throw runtime_error("unexpected '(' without function name");
                    }
                    current_function = word;
                    word.clear();
                    args.clear();
                    paren_depth = 0;
                    state = State::Args;
                } else if (c == '#') {
                    if (!word.empty()) {
                        throw runtime_error(
                            "unexpected '#' after identifier '" + word + "'");
                    }
                    state = State::Comment;
                } else {
                    throw runtime_error(string("unexpected character: ") + c);
                }
                break;

            case State::ExpectingOpenParen:
                if (isspace(c)) {
                    // skip
                } else if (c == '(') {
                    args.clear();
                    paren_depth = 0;
                    state = State::Args;
                } else {
                    throw runtime_error(string("expected '(' but got '") + c + "'");
                }
                break;

            case State::Args:
                if (c == '(') {
                    args += c;
                    paren_depth++;
                } else if (c == ')') {
                    if (paren_depth == 0) {
                        execute(current_function, split_args(args));
                        args.clear();
                        current_function.clear();
                        state = State::ExpectingSemicolon;
                    } else {
                        args += c;
                        paren_depth--;
                    }
                } else {
                    args += c;
                }
                break;

            case State::ExpectingSemicolon:
                if (isspace(c)) {
                    // skip
                } else if (c == ';') {
                    state = State::Word;
                } else if (c == '#') {
                    state = State::Comment;
                } else {
                    throw runtime_error(string("expected ';' but got '") + c + "'");
                }
                break;

            case State::Comment:
                if (c == ';') {
                    state = State::Word;
                }
                break;
        }
    }

    if (!word.empty()) {
        throw runtime_error("unexpected end of file: '" + word + "' is incomplete");
    }

    switch (state) {
        case State::Word: break;
        case State::ExpectingOpenParen:
            throw runtime_error("unexpected end of file: expected '('");
        case State::Args:
            throw runtime_error("unexpected end of file: expected ')'");
        case State::ExpectingSemicolon:
            throw runtime_error("unexpected end of file: expected ';'");
        case State::Comment:
            throw runtime_error("unexpected end of file inside comment: expected ';'");
    }
}

int main() {
    if (!inputFile.is_open()) {
        cerr << "error: could not open input.txt\n";
        return 1;
    }

    try {
        run_source(inputFile);
    } catch (const exception& e) {
        cerr << "error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}