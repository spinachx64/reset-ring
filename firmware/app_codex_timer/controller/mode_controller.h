#pragma once
namespace quota {
class ModeController {
public:
    void load();
    void toggle();
    int mode() const { return current; }
private:
    int current = 0;
};
}
