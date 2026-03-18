#include <iostream>

using namespace std;


template <typename... Args>
void print(Args&&... args) {
    // Fold expression (C++17) to print all arguments separated by spaces
    (std::cout << ... << args) << std::endl;
}

// Usage:
// log("The result is: ", 42, " (Hex: ", 0x2A, ")");

template <typename T, typename Func>
void for_range(T start, T end, Func action) {
    for (T i = start; i < end; ++i) {
        action(i);
    }
}

template <typename Func>
void secure_run(Func action) {
    try {
        action();
    } catch (const std::exception& e) {
        std::cerr << "Automated Error Capture: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error occurred." << std::endl;
    }
}

template <typename T, typename TransformFunc>
auto map_vec(const std::vector<T>& input, TransformFunc func) {
    using ReturnType = decltype(func(std::declval<T>()));
    std::vector<ReturnType> output;
    output.reserve(input.size());

    for (const auto& item : input) {
        output.push_back(func(item));
    }
    return output;
}

enum class Comparison { Equal, NotEqual, Greater, Less };
bool evaluate(int a, int b, Comparison op) {
    switch (op) {
        case Comparison::Equal:    return a == b;
        case Comparison::NotEqual: return a != b;
        case Comparison::Greater:  return a > b;
        case Comparison::Less:     return a < b;
        default:                   return false;
    }
}
