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

## Duplex-Hunter-UI

A static html page was built to provide an overview of the generated results. 
Open `docs/duplex-hunter-ui.html` in your local bronswer, load export results and view a summary.

![Results Selection Page](docs/duplex-hunter-ui-selection-page.png)
![Results Presentation Page](docs/duplex-hunter-ui-results-page.png)
