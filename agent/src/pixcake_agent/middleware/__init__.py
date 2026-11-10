"""PixCake-specific Agent guards.

The C++ gateway remains the final authority; these guards only keep malformed
or obviously unsafe model calls from reaching it.
"""

from .guards import LoopBudget, validate_tool_name

__all__ = ["LoopBudget", "validate_tool_name"]
