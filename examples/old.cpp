#include <iostream>
#include <vector>
#include <string>

// keeps the last few commands the user typed
class History {
public:
    explicit History(size_t limit) : limit_(limit) {}

    void add(const std::string& cmd) {
        items_.push_back(cmd);
        if (items_.size() > limit_) {
            items_.erase(items_.begin());
        }
    }

    void print() const {
        for (size_t i = 0; i < items_.size(); i++) {
            std::cout << i + 1 << ": " << items_[i] << "\n";
        }
    }

private:
    size_t limit_;
    std::vector<std::string> items_;
};

int main() {
    History h(3);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "history") {
            h.print();
        } else {
            h.add(line);
        }
    }
    return 0;
}
