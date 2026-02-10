# Prompt Module (`modules/prompt`)

The prompt module provides Nazg’s conversational CLI experience. It builds rich terminal cards with greetings,
facts, tips, and action menus so the assistant feels like a teammate instead of a stream of plain text.

---

## 1. Goals

- **Expressive UI** – Make it easy to present status cards, fact lists, and menus with consistent styling.
- **Developer-friendly API** – Offer a fluent builder interface that keeps call sites readable.
- **Terminal awareness** – Detect colour/emoji support and fall back gracefully when unavailable.
- **Reusability** – Share the same rendering code across assistant mode, git helpers, package installers, and
  future interactive commands.

---

## 2. Core Types

| Type | Description |
|------|-------------|
| `prompt::Prompt` | Main builder used to compose titles, questions, facts, info lines, and actions. |
| `prompt::Style` | Rendering mode (`STANDARD`, `VERBOSE`, `MINIMAL`). |
| `prompt::Colors` / `prompt::Icons` | Encapsulated colour palettes and glyphs; automatically chosen based on terminal detection. |

`modules/prompt/include/prompt/` contains the headers, with implementations in `modules/prompt/src/`.

---

## 3. Usage Pattern

```cpp
prompt::Prompt assistant(log);
assistant.title("nazg assistant")
         .question("👋 Hi! I'm Nazg. How can I help?")
         .fact("Current directory", dir_name)
         .fact("Language", project_info.language)
         .info("This directory looks empty. Want to scaffold a project?")
         .action("🚀 Create a new project (C++/Python/C)");

int choice = assistant.choice(actions, default_index);
if (choice == -1) {
  // user cancelled
}
```

Builder methods return references so they can be chained. Rendering happens when `choice`, `confirm`, or other
terminal-interaction methods are called.

---

## 4. Features

- **Title & question** – Set the headline and lead-in for the card.
- **Facts** – Display key/value pairs aligned for readability.
- **Info messages** – Add additional context or tips.
- **Status line** – Highlight an important state (used by some commands for alerts).
- **Action menus** – Present numbered options; `choice` handles user input and returns the selected index (or `-1`
  on cancel/EOF).
- **Styles** – Switch between `STANDARD`, `VERBOSE`, and `MINIMAL` to fit the user’s environment. Minimal mode
  avoids colour/emoji for automation or restricted terminals.
- **Confirmation helpers** – `confirm()` displays a yes/no prompt; `force_yes`/`force_no` let scripts override the
  interaction for non-interactive contexts.

---

## 5. Assistant Integration

Assistant mode relies on `prompt::Prompt` to:
- Render workspace facts (directory, language, git status, divergences).
- Present actionable options (status, build, commit, scaffold, update, exit).
- Provide contextual tips (“Looks empty—want to scaffold a project?”).
- Capture the user’s selection while showing how to execute the chosen command manually.

Other modules reuse the same builder for friendly confirmations (e.g., `git-init`, package installation prompts).

---

## 6. Demo Command

`prompt::register_demo_command(registry)` registers `nazg prompt-demo`, a showcase for the prompt features. Run
it to see cards, facts, and choice flows in isolation—useful when designing new assistant experiences.

---

## 7. Terminal Considerations

- Colour and emoji usage depend on terminal capability checks inside the module.
- When colour is disabled (either by detection or config), prompts still render clean text without control codes.
- Unicode output assumes UTF-8; provide a minimal style fallback or disable emoji in environments with different
  encodings.

---

## 8. Extending the API

To add new UI patterns:
1. Extend `prompt::Prompt` with a dedicated method (e.g., `warning`, `success` badges).
2. Update the renderer so each style handles the new section gracefully.
3. Keep defaults backwards compatible so existing prompts do not change unexpectedly.
4. Document the new method here and add an example to `prompt-demo` for discoverability.

---

## 9. Troubleshooting

| Issue | Cause | Fix |
|-------|-------|----|
| Raw ANSI codes appear | Terminal lacks colour support | Switch to `MINIMAL` style or disable colour via config. |
| `choice` always returns `-1` | Input stream closed or non-interactive shell | Detect the scenario and fall back to non-interactive behaviour. |
| Emoji render as boxes | Terminal not using UTF-8 or font lacks glyphs | Provide ASCII alternatives or allow `--no-emoji` toggles. |

The prompt module defines Nazg’s tone. Reuse it for every interactive command to keep the assistant coherent and
accessible.
