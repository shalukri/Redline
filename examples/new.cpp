#include <deque>
#include <iostream>
#include <string>

// keeps the last few commands the user typed
class History {
public:
    explicit History(size_t limit) : limit_(limit) {}

    void add(const std::string& cmd) {
        if (cmd.empty()) return;   // ignore blank lines
        items_.push_back(cmd);
        if (items_.size() > limit_) {
            items_.pop_front();
        }
    }

    void print() const {
        for (size_t i = 0; i < items_.size(); i++) {
            std::cout << i + 1 << ": " << items_[i] << "\n";
        }
    }

    size_t size() const { return items_.size(); }

private:
    size_t limit_;
    std::deque<std::string> items_;
};

int main() {
    History h(5);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "history") {
            h.print();
        } else if (line == "count") {
            std::cout << h.size() << " commands stored\n";
        } else {
            h.add(line);
        }
    }
    return 0;
}
