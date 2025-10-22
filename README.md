# Duplex-Hunter: Duplicate & Unique Scanner

Simple tool to recursively scan directories, hash files, and find duplicates.
Supports fast or full hashing and exports results to JSON.

## Usage
```bash
duplexhunter [options]

--path <path>           Path to scan (can be repeated)
--depth <n>             Max recursion depth
--export-path <path>    Directory to save results
--enable-fast-hash      Use faster (partial) hashing
--help                  Show help
```