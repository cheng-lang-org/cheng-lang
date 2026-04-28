# File And Process IO

Modules: `std/os`, `std/syncio`, `std/streams`

Key APIs: `readFile/readFileInto/writeFile/writeFileBytes`, `fileExists/dirExists/createDir/removeFile/renameFile`, `joinPath/splitFile/walkDir`, `execCmdEx/execFileCapture`

Example: `examples/std/file_io.cheng`

Notes: The v1.1 baseline only checks local filesystem and local process capture behavior. It does not introduce new sandbox or permission semantics.
