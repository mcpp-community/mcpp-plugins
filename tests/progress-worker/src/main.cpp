import std;
import mcpp.plugins.progress.writer;
int main(int argc, char**) {
    const bool probe = argc > 1;
    mcpp::plugins::progress::writer writer;
    if (!writer.enabled()) return probe ? 3 : 0;
    const mcpp::plugins::progress::snapshot start{.phase="configure"};
    const bool began = writer.report(start);
    std::this_thread::sleep_for(std::chrono::milliseconds(210));
    const bool finished = writer.report({.phase="compile",.done=1,.total=1});
    return probe && (!began || !finished) ? 2 : 0;
}
