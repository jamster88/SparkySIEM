#!/usr/bin/env python3
"""Renders a Claude Code session transcript into readable Markdown.

Used to produce misc/chat-transcript.md and misc/terminal-commands.md from the
session JSONL. Kept in the repo so the extraction is reproducible rather than a
one-off paste.

Usage: extract_session.py <session.jsonl> <out-chat.md> <out-commands.md>
"""
import json
import sys

RESULT_LIMIT = 900  # characters of each tool result to keep


def load(path):
    entries = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                entries.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return entries


def blocks(entry):
    """Yields the content blocks of an assistant/user message entry."""
    content = (entry.get("message") or {}).get("content")
    if isinstance(content, str):
        # A plain-text user turn.
        yield {"type": "text", "text": content}
    elif isinstance(content, list):
        for block in content:
            if isinstance(block, dict):
                yield block


def clip(text, limit=RESULT_LIMIT):
    text = text.rstrip()
    if len(text) <= limit:
        return text
    return text[:limit] + "\n... [truncated]"


def result_text(block):
    content = block.get("content")
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        parts = [c.get("text", "") for c in content
                 if isinstance(c, dict) and c.get("type") == "text"]
        return "\n".join(parts)
    return ""


def tool_summary(block):
    """One-line description of a tool call, plus optional detail body."""
    name = block.get("name", "?")
    args = block.get("input") or {}
    if name in ("Bash", "Monitor"):
        return name, args.get("description") or args.get("command", ""), args.get("command", "")
    if name in ("Read", "Write", "Edit", "NotebookEdit"):
        return name, args.get("file_path", ""), ""
    if name in ("TaskCreate", "TaskUpdate", "TaskGet"):
        return name, args.get("subject") or args.get("taskId", ""), ""
    if name == "Agent":
        return name, args.get("description", ""), ""
    flat = json.dumps(args)
    return name, flat[:160], ""


def render(entries, chat_path, commands_path):
    chat = ["# SparkySIEM - session transcript",
            "",
            "Verification and repair of the FileMonitor, plus the GoogleTest suite.",
            "Tool results are truncated to keep this readable; the untruncated record is",
            "the session JSONL under `~/.claude/projects/`.",
            ""]
    commands = ["# Every terminal command run during the session",
                "",
                "In order, exactly as executed. `#` lines are the description that",
                "accompanied each call. Commands run inside containers appear as the",
                "`docker run`/`docker exec` invocation that carried them.",
                "",
                "```sh"]

    results = {}
    for entry in entries:
        for block in blocks(entry):
            if block.get("type") == "tool_result":
                results[block.get("tool_use_id")] = result_text(block)

    turn = 0
    for entry in entries:
        kind = entry.get("type")
        if kind not in ("user", "assistant"):
            continue

        for block in blocks(entry):
            btype = block.get("type")

            if btype == "text":
                text = (block.get("text") or "").strip()
                if not text:
                    continue
                if kind == "user":
                    turn += 1
                    chat.append(f"\n## Turn {turn} - user\n")
                else:
                    chat.append("\n### assistant\n")
                chat.append(text)
                chat.append("")

            elif btype == "thinking":
                continue  # Reasoning is not part of the deliverable record.

            elif btype == "tool_use":
                name, label, command = tool_summary(block)
                chat.append(f"\n**{name}** - {label}\n")
                if command:
                    chat.append("```sh")
                    chat.append(command.strip())
                    chat.append("```")
                    commands.append("")
                    commands.append(f"# {label}")
                    commands.append(command.strip())
                out = results.get(block.get("id"))
                if out and out.strip():
                    chat.append("<details><summary>result</summary>\n")
                    chat.append("```")
                    chat.append(clip(out))
                    chat.append("```")
                    chat.append("\n</details>")
                chat.append("")

    commands.append("```")

    with open(chat_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(chat) + "\n")
    with open(commands_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(commands) + "\n")

    return sum(1 for line in commands if line and not line.startswith(("#", "```")))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    count = render(load(sys.argv[1]), sys.argv[2], sys.argv[3])
    print(f"wrote {sys.argv[2]} and {sys.argv[3]} ({count} command lines)")
