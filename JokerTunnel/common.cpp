#include <string>
#include <random>
std::string generateRandomId(size_t length = 0) {
    static const std::string allowed_chars{ "123456789BCDFGHJKLMNPQRSTVWXZbcdfghjklmnpqrstvwxz" };
    static thread_local std::default_random_engine randomEngine(std::random_device{}());
    static thread_local std::uniform_int_distribution<int>
        randomDistribution(0, allowed_chars.size() - 1);
    std::string id(length ? length : 32, '\0');
    for (std::string::value_type&
        c : id) {
        c = allowed_chars[randomDistribution(randomEngine)];
    }
    return id;
}