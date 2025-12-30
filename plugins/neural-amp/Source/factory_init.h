#pragma once
// Force factory registration by ensuring the translation units with static
// Helper objects are linked. This works around static initialization order
// issues when NAM Core is built as a static library.

namespace nam
{
// Call this function before loading any models to ensure all factories
// are registered. In practice, calling it at plugin startup is sufficient.
void initializeFactories();
}
