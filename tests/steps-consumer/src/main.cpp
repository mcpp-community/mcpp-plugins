import std;

// The program exists so the fixture has something to link. What is tested ran
// before it: the step programs build.mcpp declared, and the check of their tree.
int main() {
    std::cout << "steps-consumer ok\n";
    return 0;
}
