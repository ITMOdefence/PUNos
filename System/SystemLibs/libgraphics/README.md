# libgraphics

Public graphics device ABI with a first software framebuffer backend. It
exposes one virtual mobile output, imports shared-memory frames read-only and
reports nonblocking present completion through `eventfd`.

The contract is documented in `DevDoc/SystemLibs/libgraphics.md`.
