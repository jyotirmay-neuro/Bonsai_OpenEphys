# Project Rules

## Code Navigation — Code Graph First

**Always use the code graph MCP tools before reading files or grepping the codebase.**

Priority order for navigation:
1. `mcp__plugin_code-graph-mcp_code-graph__project_map` — session start or architecture overview
2. `mcp__plugin_code-graph-mcp_code-graph__semantic_code_search` — fuzzy concept search, no known symbol
3. `mcp__plugin_code-graph-mcp_code-graph__get_ast_node` — known symbol: signature, location, blast radius
4. `mcp__plugin_code-graph-mcp_code-graph__get_call_graph` — call chains ("who calls X?")
5. `mcp__plugin_code-graph-mcp_code-graph__find_references` — rename/remove audits
6. `mcp__plugin_code-graph-mcp_code-graph__module_overview` — directory or file symbol listing
7. `mcp__plugin_code-graph-mcp_code-graph__ast_search` — typed filters (all fns returning T, etc.)

Only fall back to `Grep` for exact string/regex literals. Only `Read` files you are about to edit.

This rule applies everywhere: superpowers skills, planning, debugging, code review, implementation — all code navigation goes through the graph first.

## Code Graph DB

Index lives at `.code-graph/index.db` (relative paths — no re-index needed after repo moves).
