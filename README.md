# Shell-ish (COMP304)

```txt
https://github.com/ptanju23/COMP-304---Project-1.git
```

Shell-ish is a small Unix-like shell written in **C**. It reads a command line, parses it into commands/arguments, and executes programs using `fork()` + `execv()`.

---

## Features

### Running external programs (Part 1)
- Runs external programs by searching for executables via `PATH`.

### I/O Redirection (Part 2A)
Supported redirections:
- Input: `< file`
- Output: `> file`
- Append: `>> file` *(if implemented in your parser)*

### Pipelines (Part 2B)
Supports pipelines with one or more stages:
```sh
cmd1 | cmd2 | cmd3
```

### Commands implemented inside the shell (Part 3)

#### `cut`
Entering `cut` runs a simplified version of the Unix `cut` command.

#### `chatroom`
Creates a small “chatroom” using named pipes (FIFOs). Messages are sent over the FIFOs.

---

## Custom Command: `trash`

### What it does
`trash`, like the desktop trash, stores items deemed trash. It works over the command `trash`, and if `trash restore` is typed, it restores the trashed file requested (selected by best fit) into the current directory of the user.

Trash directory:
```txt
~/.shellish_trash/
```

This makes it possible to restore files later (limited restore behavior described below).

---

## Usage

### 1) Move files to trash
```sh
trash file1 file2 file3
```

This moves each file into `~/.shellish_trash/` using a unique stored name:
```txt
<basename>__<epoch>_<pid>_<counter>
```

Example:
```txt
notes.txt might become notes.txt__1700000000_4123_1
```

---

### 2) List files currently in trash
```sh
trash ls
```

Prints stored file entries currently in the trash directory (one per line).

Because `trash` runs in the same execution path as other commands, it also works with redirection/pipes:
```sh
trash ls > trash_list.txt
trash ls | wc
```

---

### 3) Restore a file (to current directory)
```sh
trash restore <name>
```

Example:
```sh
trash restore notes.txt
```

Restore behavior:
- The shell searches `~/.shellish_trash/` for entries starting with `notes.txt__`
- If multiple matches exist, it restores the newest one (highest timestamp)
- The restored file is placed into the current working directory as `notes.txt`

---

## Notes / Limitations
- Restore does **not** track original full paths. Files restore into the **current directory**.
- If a file with the same name already exists in the current directory, restore will fail (to avoid overwriting).
- `trash ls` shows stored names (including the timestamp suffix). This is intentional to keep the implementation simple and avoid an index file.
