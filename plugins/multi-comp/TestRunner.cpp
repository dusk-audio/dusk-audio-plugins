/**
 * Test Runner for Multi-Comp Unit Tests
 *
 * Runs all registered JUCE unit tests including:
 * - Parameter validation
 * - Gain reduction tests for all compressor modes
 * - DSP stability (NaN/Inf)
 * - Thread safety
 * - Mix knob phase alignment tests
 *
 * Build with: cmake -DBUILD_MULTI_COMP_TESTS=ON ..
 * Run with: ./MultiCompTests
 */

#include <JuceHeader.h>
#include <iostream>

int main(int argc, char* argv[])
{
    // Initialize JUCE application (needed for message manager)
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Parse command line arguments
    bool runAll = false;
    bool verbose = false;
    juce::String categoryFilter;

    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);
        if (arg == "--all" || arg == "-a")
            runAll = true;
        else if (arg == "--verbose" || arg == "-v")
            verbose = true;
        else if (arg.startsWith("--category="))
            categoryFilter = arg.fromFirstOccurrenceOf("=", false, false);
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Multi-Comp Unit Test Runner\n";
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "\nOptions:\n";
            std::cout << "  --all, -a           Run all tests (default if no category specified)\n";
            std::cout << "  --verbose, -v       Verbose output\n";
            std::cout << "  --category=NAME     Run tests in specific category\n";
            std::cout << "  --help, -h          Show this help\n";
            std::cout << "\nCategories: Compressor\n";
            return 0;
        }
    }

    // Default to running all tests
    if (!runAll && categoryFilter.isEmpty())
        runAll = true;

    // Create test runner
    juce::UnitTestRunner runner;

    // Set up logging
    runner.setAssertOnFailure(false);  // Don't abort on failures, just report

    std::cout << "=== Multi-Comp Unit Tests ===\n\n";

    // Run tests
    if (runAll)
    {
        std::cout << "Running all tests...\n\n";
        runner.runAllTests();
    }
    else if (categoryFilter.isNotEmpty())
    {
        std::cout << "Running tests in category: " << categoryFilter << "\n\n";
        runner.runTestsInCategory(categoryFilter);
    }

    // Print results
    std::cout << "\n=== Test Results ===\n\n";

    int totalPasses = 0;
    int totalFailures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* result = runner.getResult(i);

        totalPasses += result->passes;
        totalFailures += result->failures;

        if (result->failures > 0 || verbose)
        {
            std::cout << "Test: " << result->unitTestName << "\n";
            std::cout << "  Passes: " << result->passes << "\n";
            std::cout << "  Failures: " << result->failures << "\n";

            // Print failure messages
            for (const auto& msg : result->messages)
            {
                std::cout << "  " << msg << "\n";
            }
            std::cout << "\n";
        }
    }

    std::cout << "=== Summary ===\n";
    std::cout << "Total Passes: " << totalPasses << "\n";
    std::cout << "Total Failures: " << totalFailures << "\n";

    if (totalFailures == 0)
        std::cout << "\nAll tests PASSED!\n";
    else
        std::cout << "\n" << totalFailures << " test(s) FAILED.\n";

    return totalFailures > 0 ? 1 : 0;
}
